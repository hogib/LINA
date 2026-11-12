// SPDX-License-Identifier: GPL-3.0-or-later
// Headless benchmark. A QQuickWindow driven by QQuickRenderControl renders the
// real QML scene into an OpenGL FBO; polish, sync and render are called by hand
// and timed one by one. Input is synthesized QMouseEvent/QWheelEvent/QKeyEvent
// sent to the window, so the normal Qt Quick event delivery and the items' own
// hit testing are exercised. No window is ever shown.
#include "bench.hpp"

#include "arrangement_item.hpp"
#include "controller.hpp"
#include "piano_roll_item.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int W = 2560, H = 1440;
constexpr int WARMUP = 20;

using Clock = std::chrono::steady_clock;
double msBetween(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Sample {
  double eventsMs = 0, polishMs = 0, syncMs = 0, renderMs = 0, finishMs = 0;
  double upnMs = 0, loopMs = 0, labelsMs = 0;
  int vertsTotal = 0, vertsRebuilt = 0, items = 0, labels = 0, waveCols = 0;
  int geomNodes = 0, textNodes = 0;
  bool rebuilt = false;
  /// CPU time of the GUI/render thread for this frame, GPU completion excluded.
  double cpu() const { return eventsMs + polishMs + syncMs + renderMs; }
  /// The part that corresponds to what the egui bench measured (no renderer).
  double ui() const { return eventsMs + polishMs + syncMs; }
};
using Samples = std::vector<Sample>;

struct Stat {
  double mean = 0, p95 = 0, max = 0;
};
template <typename F> Stat stat(const Samples &s, F &&f) {
  if (s.empty())
    return {};
  std::vector<double> v;
  v.reserve(s.size());
  for (const Sample &x : s)
    v.push_back(f(x));
  std::ranges::sort(v);
  double sum = 0;
  for (const double x : v)
    sum += x;
  const auto idx = std::clamp<std::size_t>(
      static_cast<std::size_t>(std::ceil(static_cast<double>(v.size()) * 0.95)),
      1, v.size());
  return {sum / static_cast<double>(v.size()), v[idx - 1], v.back()};
}

class Harness {
public:
  AppController ctl{1};
  QOpenGLContext ctx;
  QOffscreenSurface surf;
  QQuickRenderControl *rc = nullptr;
  QQuickWindow *win = nullptr;
  QQmlEngine *engine = nullptr;
  QOpenGLFramebufferObject *fbo = nullptr;
  QQuickItem *root = nullptr;
  ArrangementItem *arr = nullptr;
  PianoRollItem *pr = nullptr;
  sg::ItemStats *active = nullptr;
  QString glVendor, glRenderer, glVersion;

  bool init() {
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    ctx.setFormat(fmt);
    if (!ctx.create()) {
      std::fprintf(stderr, "bench: QOpenGLContext::create failed\n");
      return false;
    }
    surf.setFormat(ctx.format());
    surf.create();
    if (!ctx.makeCurrent(&surf)) {
      std::fprintf(stderr, "bench: makeCurrent failed\n");
      return false;
    }
    auto *gl = ctx.functions();
    const auto str = [&](GLenum name) {
      return QString::fromLatin1(
          reinterpret_cast<const char *>(gl->glGetString(name)));
    };
    glVendor = str(GL_VENDOR);
    glRenderer = str(GL_RENDERER);
    glVersion = str(GL_VERSION);

    rc = new QQuickRenderControl;
    win = new QQuickWindow(rc);
    win->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&ctx));
    win->setColor(Qt::black);
    win->setGeometry(0, 0, W, H);
    if (!rc->initialize()) {
      std::fprintf(stderr, "bench: QQuickRenderControl::initialize failed\n");
      return false;
    }
    ctx.makeCurrent(&surf);
    fbo = new QOpenGLFramebufferObject(
        QSize(W, H), QOpenGLFramebufferObject::CombinedDepthStencil);
    win->setRenderTarget(
        QQuickRenderTarget::fromOpenGLTexture(fbo->texture(), QSize(W, H)));

    engine = new QQmlEngine;
    QQmlComponent comp(engine, "Lina", "Main");
    QObject *obj = comp.createWithInitialProperties(
        {{"controller", QVariant::fromValue(&ctl)}});
    root = qobject_cast<QQuickItem *>(obj);
    if (!root) {
      std::fprintf(stderr, "bench: QML load failed: %s\n",
                   qPrintable(comp.errorString()));
      return false;
    }
    root->setParentItem(win->contentItem());
    root->setSize(QSizeF(W, H));
    arr = root->findChild<ArrangementItem *>("arrangement");
    pr = root->findChild<PianoRollItem *>("pianoRoll");
    if (!arr || !pr) {
      std::fprintf(stderr, "bench: custom items not found in the scene\n");
      return false;
    }
    return true;
  }

  ~Harness() {
    ctx.makeCurrent(&surf);
    delete root;
    delete rc;
    delete win;
    delete engine;
    delete fbo;
    ctx.doneCurrent();
  }

  /// One frame: input + transport tick, then polish / sync / render / glFinish.
  void frame(const std::function<void()> &input, Samples *out) {
    if (active)
      active->calls = 0;
    const auto t0 = Clock::now();
    if (input)
      input();
    ctl.advance(1.0 / 60.0);
    QCoreApplication::processEvents();
    const auto t1 = Clock::now();
    rc->polishItems();
    const auto t2 = Clock::now();
    rc->beginFrame();
    rc->sync();
    const auto t3 = Clock::now();
    rc->render();
    rc->endFrame();
    const auto t4 = Clock::now();
    if (QOpenGLContext::currentContext() != &ctx)
      ctx.makeCurrent(&surf);
    ctx.functions()->glFinish();
    const auto t5 = Clock::now();
    if (!out)
      return;
    Sample s;
    s.eventsMs = msBetween(t0, t1);
    s.polishMs = msBetween(t1, t2);
    s.syncMs = msBetween(t2, t3);
    s.renderMs = msBetween(t3, t4);
    s.finishMs = msBetween(t4, t5);
    if (active) {
      if (active->calls > 0) {
        s.upnMs = active->upnMs;
        s.loopMs = active->loopMs;
        s.labelsMs = active->labelsMs;
        s.vertsRebuilt = active->vertsRebuilt;
        s.rebuilt = active->contentRebuilt;
      }
      s.vertsTotal = active->vertsTotal;
      s.items = active->items;
      s.labels = active->labels;
      s.waveCols = active->waveCols;
      s.geomNodes = active->geomNodes;
      s.textNodes = active->textNodes;
    }
    out->push_back(s);
  }

  void mouseScene(QEvent::Type type, QPointF scene, Qt::MouseButton button,
                  Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = {}) {
    QMouseEvent ev(type, scene, scene, button, buttons, mods);
    QCoreApplication::sendEvent(win, &ev);
  }
  void mouse(QEvent::Type type, QQuickItem *item, QPointF local,
             Qt::MouseButton button, Qt::MouseButtons buttons,
             Qt::KeyboardModifiers mods = {}) {
    mouseScene(type, item->mapToScene(local), button, buttons, mods);
  }
  void wheel(QQuickItem *item, QPointF local, QPoint pixelDelta,
             Qt::KeyboardModifiers mods) {
    const QPointF scene = item->mapToScene(local);
    QWheelEvent ev(scene, scene, pixelDelta, QPoint(), Qt::NoButton, mods,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(win, &ev);
  }
  void key(int key, Qt::KeyboardModifiers mods = {}) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(win, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QCoreApplication::sendEvent(win, &release);
  }
  void click(QPointF scene) {
    mouseScene(QEvent::MouseMove, scene, Qt::NoButton, Qt::NoButton);
    mouseScene(QEvent::MouseButtonPress, scene, Qt::LeftButton, Qt::LeftButton);
    mouseScene(QEvent::MouseButtonRelease, scene, Qt::LeftButton, Qt::NoButton);
  }
  void shot(const QString &dir, const QString &name) {
    if (dir.isEmpty())
      return;
    ctx.makeCurrent(&surf);
    QImage img = fbo->toImage();
    img.save(QDir(dir).filePath(name + ".png"));
  }
};

struct Zoom {
  const char *name;
  double bars;
  double rows;
  bool center;
};

const Zoom ARR_ZOOMS[] = {
    {"whole project (64 tracks)", 1e9, 64.0, false},
    {"medium (32 bars x 24 tracks)", 32.0, 24.0, true},
    {"close (4 bars x 10 tracks)", 4.0, 10.0, true},
};
const Zoom PR_ZOOMS[] = {
    {"whole clip (256 bars x 128 keys)", 1e9, 128.0, false},
    {"medium (16 bars x 48 keys)", 16.0, 48.0, true},
    {"close (2 bars x 30 keys)", 2.0, 30.0, true},
};

/// What the scripted gestures need from a view, so both views share one script.
struct ViewOps {
  QQuickItem *item = nullptr;
  sg::ItemStats *stats = nullptr;
  QRectF body;
  std::function<void(const Zoom &)> setView;
  std::function<std::optional<QPointF>(bool)> target;
  std::function<std::optional<QPointF>()> empty; ///< null: no rubber band phase
  std::function<void()> clearSelection;
  std::function<QString()> selectionInfo;
};

struct Phase {
  std::string name;
  Samples samples;
};
struct CaseResult {
  std::vector<Phase> phases;
  std::vector<std::string> notes;
};

void drag(Harness &h, const ViewOps &v, QPointF from, QPointF perFrame,
          int frames, Samples &out, const std::function<void()> &atMid = {}) {
  h.frame(
      [&] {
        h.mouse(QEvent::MouseMove, v.item, from, Qt::NoButton, Qt::NoButton);
      },
      &out);
  h.frame(
      [&] {
        h.mouse(QEvent::MouseButtonPress, v.item, from, Qt::LeftButton,
                Qt::LeftButton);
      },
      &out);
  QPointF pos = from;
  for (int i = 0; i < frames - 4; ++i) {
    pos += perFrame;
    h.frame(
        [&] {
          h.mouse(QEvent::MouseMove, v.item, pos, Qt::NoButton, Qt::LeftButton);
        },
        &out);
    if (atMid && i == (frames - 4) / 2)
      atMid();
  }
  h.frame(
      [&] {
        h.mouse(QEvent::MouseButtonRelease, v.item, pos, Qt::LeftButton,
                Qt::NoButton);
      },
      &out);
  h.frame({}, &out);
}

CaseResult runCase(Harness &h, const ViewOps &v, const Zoom &z,
                   const BenchOptions &opt, const QString &shotPrefix) {
  CaseResult r;
  const int n = opt.phaseFrames;
  h.active = v.stats;
  const auto reset = [&] {
    h.ctl.reset();
    v.clearSelection();
    v.setView(z);
    h.frame({}, nullptr);
    h.frame({}, nullptr);
  };
  reset();
  const QPointF center = v.body.center();
  h.mouse(QEvent::MouseMove, v.item, center, Qt::NoButton, Qt::NoButton);
  for (int i = 0; i < WARMUP; ++i)
    h.frame({}, nullptr);
  if (!shotPrefix.isEmpty())
    h.shot(opt.shotsDir, shotPrefix);

  // Phase 1: wheel scrolling, out and back, both axes.
  {
    Phase p{"scroll", {}};
    for (int i = 0; i < n; ++i) {
      const int sign = i < n / 2 ? -1 : 1;
      h.frame(
          [&] { h.wheel(v.item, center, QPoint(40 * sign, 15 * sign), {}); },
          &p.samples);
      if (!shotPrefix.isEmpty() && i == n / 2 - 1)
        h.shot(opt.shotsDir, shotPrefix + "_scrolled");
    }
    r.phases.push_back(std::move(p));
  }
  // Phase 2: ctrl+wheel zoom around the cursor, in and back out.
  {
    reset();
    Phase p{"zoom", {}};
    for (int i = 0; i < n; ++i) {
      const int sign = i < n / 2 ? 1 : -1;
      h.frame(
          [&] {
            h.wheel(v.item, center, QPoint(0, 4 * sign), Qt::ControlModifier);
          },
          &p.samples);
    }
    r.phases.push_back(std::move(p));
  }
  // Phase 3: drag-move an item. Phase 4: drag-resize an item.
  for (const bool edge : {false, true}) {
    reset();
    const char *what = edge ? "resize" : "move";
    Phase p{what, {}};
    if (const auto target = v.target(edge)) {
      const int before = h.ctl.applied();
      const auto mid = [&] {
        if (!shotPrefix.isEmpty())
          h.shot(opt.shotsDir, shotPrefix + "_" + what);
      };
      drag(h, v, *target, edge ? QPointF(1.5, 0.0) : QPointF(2.5, 0.8), n,
           p.samples, mid);
      r.notes.push_back(std::string(what) + ": " +
                        std::to_string(h.ctl.applied() - before) + " cmd -> " +
                        h.ctl.lastCommand().toStdString());
    } else {
      r.notes.push_back(std::string(what) +
                        ": NO TARGET FOUND, phase replaced by idle frames");
      for (int i = 0; i < n; ++i)
        h.frame({}, &p.samples);
    }
    r.phases.push_back(std::move(p));
  }
  // Phase 5 (piano roll only): rubber-band selection.
  if (v.empty) {
    reset();
    Phase p{"band", {}};
    if (const auto from = v.empty()) {
      const auto mid = [&] {
        if (!shotPrefix.isEmpty())
          h.shot(opt.shotsDir, shotPrefix + "_band");
      };
      drag(h, v, *from, QPointF(6.0, 3.0), n, p.samples, mid);
      r.notes.push_back("band: " + v.selectionInfo().toStdString());
    } else {
      r.notes.push_back("band: NO EMPTY SPOT FOUND, phase replaced by idle");
      for (int i = 0; i < n; ++i)
        h.frame({}, &p.samples);
    }
    r.phases.push_back(std::move(p));
  }
  // Playhead only: nothing changes except the transport position.
  {
    reset();
    for (int i = 0; i < 5; ++i)
      h.frame({}, nullptr);
    Phase p{"playhead only", {}};
    for (int i = 0; i < opt.playheadFrames; ++i)
      h.frame({}, &p.samples);
    r.phases.push_back(std::move(p));
  }
  return r;
}

void printHeader() {
  std::printf(
      "  %-15s %6s | %21s | %6s %6s | %21s | %6s | %21s | %6s | %8s %16s %5s\n",
      "phase", "frames", "CPU frame ms", "events", "polish",
      "updatePaintNode ms", "sync", "render ms (CPU side)", "glFin", "verts",
      "rewritten/frame", "rebld");
  std::printf(
      "  %-15s %6s | %6s %6s %7s | %6s %6s | %6s %6s %7s | %6s | %6s %6s %7s | "
      "%6s | %8s %8s %7s %5s\n",
      "", "", "mean", "p95", "max", "mean", "mean", "mean", "p95", "max",
      "mean", "mean", "p95", "max", "mean", "held", "mean", "max", "frms");
}

void printRow(const std::string &name, const Samples &s) {
  if (s.empty())
    return;
  const Stat cpu = stat(s, [](const Sample &x) { return x.cpu(); });
  const Stat ev = stat(s, [](const Sample &x) { return x.eventsMs; });
  const Stat po = stat(s, [](const Sample &x) { return x.polishMs; });
  const Stat upn = stat(s, [](const Sample &x) { return x.upnMs; });
  const Stat sy = stat(s, [](const Sample &x) { return x.syncMs; });
  const Stat re = stat(s, [](const Sample &x) { return x.renderMs; });
  const Stat fi = stat(s, [](const Sample &x) { return x.finishMs; });
  const Stat vt = stat(s, [](const Sample &x) { return double(x.vertsTotal); });
  const Stat vr =
      stat(s, [](const Sample &x) { return double(x.vertsRebuilt); });
  const auto rebuilds =
      std::ranges::count_if(s, [](const Sample &x) { return x.rebuilt; });
  std::printf("  %-15s %6zu | %6.3f %6.3f %7.3f | %6.3f %6.3f | %6.3f %6.3f "
              "%7.3f | %6.3f | %6.3f %6.3f %7.3f | %6.3f | %8.0f %8.0f %7.0f "
              "%5td\n",
              name.c_str(), s.size(), cpu.mean, cpu.p95, cpu.max, ev.mean,
              po.mean, upn.mean, upn.p95, upn.max, sy.mean, re.mean, re.p95,
              re.max, fi.mean, vt.mean, vr.mean, vr.max, rebuilds);
}

void printCase(const char *view, const Zoom &z, const CaseResult &r) {
  std::printf("%s / %s\n", view, z.name);
  Samples gestures;
  const Samples *playhead = nullptr;
  for (const Phase &p : r.phases) {
    if (p.name == "playhead only") {
      playhead = &p.samples;
      continue;
    }
    printRow(p.name, p.samples);
    gestures.insert(gestures.end(), p.samples.begin(), p.samples.end());
  }
  printRow("ALL GESTURES", gestures);
  if (playhead)
    printRow("playhead only", *playhead);
  if (!gestures.empty()) {
    const Stat ui = stat(gestures, [](const Sample &x) { return x.ui(); });
    const Stat lo = stat(gestures, [](const Sample &x) { return x.loopMs; });
    const Stat la = stat(gestures, [](const Sample &x) { return x.labelsMs; });
    const Stat it =
        stat(gestures, [](const Sample &x) { return double(x.items); });
    const Stat lb =
        stat(gestures, [](const Sample &x) { return double(x.labels); });
    const Stat wc =
        stat(gestures, [](const Sample &x) { return double(x.waveCols); });
    const Sample &last = gestures.back();
    std::printf("  all gestures, without the renderer (events+polish+sync): "
                "mean %.3f  p95 %.3f  max %.3f ms\n",
                ui.mean, ui.p95, ui.max);
    std::printf("  inside updatePaintNode (mean): item loop %.3f ms, text "
                "layouts %.3f ms; retained content avg: %.0f items, %.0f "
                "labels, %.0f waveform columns; nodes: %d geometry + %d text\n",
                lo.mean, la.mean, it.mean, lb.mean, wc.mean, last.geomNodes,
                last.textNodes);
  }
  for (const std::string &n : r.notes)
    std::printf("  gesture check: %s\n", n.c_str());
}

// ---------------------------------------------------------------- naive
// variant

void runNaive(Harness &h, const BenchOptions &opt) {
  std::printf("\n== NAIVE VARIANT: one QML Rectangle + Text per clip (Repeater "
              "over a QAbstractListModel) ==\n");
  std::printf("No waveforms, no gestures, no culling; properties are set "
              "directly by the bench (no input events).\n");
  h.active = nullptr;
  const auto t0 = Clock::now();
  h.root->setProperty("tab", 2);
  QCoreApplication::processEvents();
  const auto t1 = Clock::now();
  auto *loader = h.root->property("naiveLoader").value<QQuickItem *>();
  auto *nv = loader ? loader->property("item").value<QQuickItem *>() : nullptr;
  if (!nv) {
    std::printf("naive: NOT LOADED\n");
    return;
  }
  Samples first;
  h.frame({}, &first);
  std::printf("delegates created: %d in %.1f ms; first frame after creation: "
              "sync %.1f ms, render %.1f ms\n",
              nv->property("count").toInt(), msBetween(t0, t1), first[0].syncMs,
              first[0].renderMs);
  std::printf("  %-34s %-14s %6s | %21s | %6s %6s %6s | %21s | %6s\n", "zoom",
              "phase", "frames", "CPU frame ms (mean/p95/max)", "events",
              "polish", "sync", "render ms (mean/p95/max)", "glFin");

  const lina::Project &p = h.ctl.project();
  const double w = nv->width(), hh = nv->height();
  const double len = static_cast<double>(p.len) * 1.02;
  const int n = opt.phaseFrames;
  bool shot = true;
  for (const Zoom &z : ARR_ZOOMS) {
    const double ppt0 =
        std::max(w / (z.bars * static_cast<double>(lina::BAR)), w / len);
    const double trackH = hh / z.rows;
    const double tick0 = z.center ? static_cast<double>(p.len) * 0.4 : 0.0;
    const double y0 = z.center ? (64 * trackH - hh) * 0.4 : 0.0;
    const auto apply = [&](double ppt, double tick, double y) {
      nv->setProperty("pxPerTick", ppt);
      nv->setProperty("trackH", trackH);
      nv->setProperty("scrollTick",
                      std::clamp(tick, 0.0, std::max(len - w / ppt, 0.0)));
      nv->setProperty("scrollY",
                      std::clamp(y, 0.0, std::max(64 * trackH - hh, 0.0)));
    };
    const auto row = [&](const char *phase, const Samples &s) {
      const Stat cpu = stat(s, [](const Sample &x) { return x.cpu(); });
      const Stat ev = stat(s, [](const Sample &x) { return x.eventsMs; });
      const Stat po = stat(s, [](const Sample &x) { return x.polishMs; });
      const Stat sy = stat(s, [](const Sample &x) { return x.syncMs; });
      const Stat re = stat(s, [](const Sample &x) { return x.renderMs; });
      const Stat fi = stat(s, [](const Sample &x) { return x.finishMs; });
      std::printf("  %-34s %-14s %6zu | %6.3f %6.3f %7.3f | %6.3f %6.3f %6.3f "
                  "| %6.3f %6.3f %7.3f | %6.3f\n",
                  z.name, phase, s.size(), cpu.mean, cpu.p95, cpu.max, ev.mean,
                  po.mean, sy.mean, re.mean, re.p95, re.max, fi.mean);
    };
    apply(ppt0, tick0, y0);
    for (int i = 0; i < WARMUP; ++i)
      h.frame({}, nullptr);
    if (shot && z.center) {
      h.shot(opt.shotsDir, "naive_medium");
      shot = false;
    }
    Samples s;
    double tick = tick0, y = y0;
    for (int i = 0; i < n; ++i) {
      const int sign = i < n / 2 ? 1 : -1;
      tick += sign * 40.0 / ppt0;
      y += sign * 15.0;
      h.frame([&] { apply(ppt0, tick, y); }, &s);
    }
    row("scroll", s);
    s.clear();
    apply(ppt0, tick0, y0);
    h.frame({}, nullptr);
    double ppt = ppt0;
    for (int i = 0; i < n; ++i) {
      const int sign = i < n / 2 ? 1 : -1;
      const double anchor = tick0 + (w / 2) / ppt0;
      ppt = std::clamp(ppt * std::exp(sign * 4.0 / 200.0), w / len, 1.0);
      h.frame([&] { apply(ppt, anchor - (w / 2) / ppt, y0); }, &s);
    }
    row("zoom", s);
    s.clear();
    apply(ppt0, tick0, y0);
    for (int i = 0; i < 5; ++i)
      h.frame({}, nullptr);
    for (int i = 0; i < opt.playheadFrames; ++i)
      h.frame({}, &s);
    row("playhead only", s);
  }
  h.root->setProperty("tab", 0);
  QCoreApplication::processEvents();
}

// ---------------------------------------------------------------- behaviour
// checks

QQuickItem *findItem(QQuickItem *root, const QString &name) {
  if (root->objectName() == name)
    return root;
  for (QQuickItem *c : root->childItems())
    if (QQuickItem *r = findItem(c, name))
      return r;
  return nullptr;
}

void runChecks(Harness &h) {
  std::printf("\n== behaviour checks through synthesized events (not a "
              "substitute for using the app) ==\n");
  // Arrangement: click selects, Delete key reaches the item, QML mute button
  // works.
  h.root->setProperty("tab", 0);
  h.root->setProperty("showHeaders", true);
  h.root->setProperty("cullDelegates", false);
  h.arr->setForceRebuild(false);
  h.ctl.reset();
  h.arr->setView(32.0, 24.0, true);
  h.frame({}, nullptr);
  const int clips = h.ctl.clipCount();
  if (const auto t = h.arr->benchTarget(false)) {
    h.click(h.arr->mapToScene(*t));
    h.frame({}, nullptr);
    const lina::ClipId sel = h.arr->selected();
    h.key(Qt::Key_Delete);
    h.frame({}, nullptr);
    std::printf("arrangement: click selected clip id %u; Delete key -> clips "
                "%d -> %d (%s), last cmd: %s\n",
                sel, clips, h.ctl.clipCount(),
                h.ctl.clipCount() == clips - 1 ? "ok" : "FAILED",
                qPrintable(h.ctl.lastCommand()));
  } else {
    std::printf("arrangement: no click target found, NOT CHECKED\n");
  }
  bool muteChecked = false;
  for (int i = 0; i < h.ctl.trackCount() && !muteChecked; ++i) {
    // Repeater delegates are not QObject children of anything reachable through
    // findChild, so the visual tree is walked instead.
    auto *btn = findItem(h.root, QStringLiteral("mute_%1").arg(i));
    if (!btn || !btn->isVisible())
      continue;
    const QPointF c = btn->mapToScene(btn->boundingRect().center());
    const QRectF pane(h.arr->mapToScene(QPointF(-190, h.arr->rulerHeight())),
                      QSizeF(190, h.arr->height() - h.arr->rulerHeight()));
    if (!pane.adjusted(0, 12, 0, -12).contains(c))
      continue;
    const bool before = h.ctl.trackMuted(i);
    h.click(c);
    h.frame({}, nullptr);
    std::printf("QML header: click on M button of track %d -> mute %d -> %d "
                "(%s), last cmd: %s\n",
                i, before, h.ctl.trackMuted(i),
                h.ctl.trackMuted(i) != before ? "ok" : "FAILED",
                qPrintable(h.ctl.lastCommand()));
    muteChecked = true;
  }
  if (!muteChecked)
    std::printf("QML header: no visible mute button found, NOT CHECKED\n");

  // Piano roll: click selects one, Ctrl+A selects all, Escape clears, Delete
  // deletes.
  h.root->setProperty("tab", 1);
  h.ctl.reset();
  h.pr->setForceRebuild(false);
  h.pr->setView(16.0, 48.0, true);
  h.frame({}, nullptr);
  if (const auto t = h.pr->benchTarget(false)) {
    h.click(h.pr->mapToScene(*t));
    const int one = h.pr->selectedCount();
    h.key(Qt::Key_A, Qt::ControlModifier);
    const int all = h.pr->selectedCount();
    h.key(Qt::Key_Escape);
    const int none = h.pr->selectedCount();
    h.click(h.pr->mapToScene(*t));
    h.key(Qt::Key_Delete);
    h.frame({}, nullptr);
    std::printf("piano roll: click -> %d selected; Ctrl+A -> %d; Escape -> %d; "
                "click + Delete -> notes %d (%s), last cmd: %s\n",
                one, all, none, h.ctl.noteCount(),
                one == 1 && all == 10000 && none == 0 &&
                        h.ctl.noteCount() == 9999
                    ? "ok"
                    : "FAILED",
                qPrintable(h.ctl.lastCommand()));
  } else {
    std::printf("piano roll: no click target found, NOT CHECKED\n");
  }
  h.ctl.reset();
}

QString cpuModel() {
  QFile f("/proc/cpuinfo");
  if (f.open(QIODevice::ReadOnly)) {
    for (const QByteArray &line : f.readAll().split('\n'))
      if (line.startsWith("model name"))
        return QString::fromLatin1(line.mid(line.indexOf(':') + 1).trimmed());
  }
  return "unknown";
}

const char *apiName(QSGRendererInterface::GraphicsApi api) {
  switch (api) {
  case QSGRendererInterface::Software:
    return "Software (QPainter rasteriser)";
  case QSGRendererInterface::OpenGL:
    return "OpenGL via QRhi";
  case QSGRendererInterface::Vulkan:
    return "Vulkan via QRhi";
  case QSGRendererInterface::Null:
    return "Null";
  default:
    return "other";
  }
}

} // namespace

int runBench(const BenchOptions &opt) {
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  Harness h;
  if (!h.init())
    return 2;
  if (!opt.shotsDir.isEmpty())
    QDir().mkpath(opt.shotsDir);
  // First frames create the scene graph nodes.
  for (int i = 0; i < 3; ++i)
    h.frame({}, nullptr);

  const auto api = h.win->rendererInterface()->graphicsApi();
  std::printf("LINA Qt Quick spike bench\n");
  std::printf("mechanism: QQuickRenderControl -> OpenGL FBO %dx%d, "
              "polish/sync/render called by hand on ONE thread, no window "
              "shown, no swap/vsync.\n",
              W, H);
  std::printf("QPA platform: %s; scene graph: %s; GL: %s | %s | %s\n",
              qPrintable(QGuiApplication::platformName()), apiName(api),
              qPrintable(h.glVendor), qPrintable(h.glRenderer),
              qPrintable(h.glVersion));
  std::printf("cpu: %s; Qt %s; build: %s; devicePixelRatio %.2f\n",
              qPrintable(cpuModel()), qVersion(),
#ifdef NDEBUG
              "release",
#else
              "DEBUG",
#endif
              h.win->effectiveDevicePixelRatio());
  std::printf("data: %d tracks, %d clips, %d notes; %d warm-up frames "
              "discarded per case; the transport advances 1/60 s in every "
              "frame of every phase.\n",
              h.ctl.trackCount(), h.ctl.clipCount(), h.ctl.noteCount(), WARMUP);
  std::printf(
      "columns: 'CPU frame' = events + polish + sync + render, all on the one "
      "thread (wall clock).\n"
      "  events  = delivery of the synthesized input to the item, its hit "
      "testing / drag logic, the model command, QML bindings of the chrome\n"
      "  updatePaintNode = our geometry building, measured inside the item; it "
      "runs inside sync and is included in the sync column\n"
      "  render  = QQuickRenderControl::render + endFrame: scene graph "
      "batching, buffer uploads, GL command submission (CPU side)\n"
      "  glFin   = glFinish after the frame: waiting for the GPU; NOT part of "
      "'CPU frame'\n"
      "  verts held = vertices in this item's geometry nodes; rewritten = "
      "vertices written by updatePaintNode that frame; rebld frms = frames "
      "that rebuilt the clip/note content geometry\n"
      "  QML chrome (toolbar, track headers / keyboard) is in the scene and in "
      "events/polish/sync/render, but not in the vertex counts.\n");

  struct Config {
    const char *name;
    bool waves;
    bool force;
    bool chrome;
    bool cull = false;
  };
  const Config arrConfigs[] = {
      {"retained; waveforms ON (waveform geometry cached between frames)", true,
       false, true},
      {"retained; waveforms OFF", false, false, true},
      {"rebuild-all every frame (immediate-mode emulation); waveforms ON "
       "(rebuilt every frame)",
       true, true, true},
      {"rebuild-all every frame (immediate-mode emulation); waveforms OFF",
       false, true, true},
      {"retained; waveforms ON; QML track headers removed", true, false, false},
      {"retained; waveforms ON; QML track headers hidden with `visible` while "
       "scrolled out (first version of the QML; default is clipped, not "
       "hidden)",
       true, false, true, true},
  };
  const Config prConfigs[] = {
      {"retained", true, false, true},
      {"rebuild-all every frame (immediate-mode emulation)", true, true, true},
      {"retained; QML keyboard column removed", true, false, false},
      {"retained; QML keys hidden with `visible` while scrolled out", true,
       false, true, true},
  };

  ViewOps arr;
  arr.item = h.arr;
  arr.stats = &h.arr->stats();
  arr.setView = [&](const Zoom &z) {
    h.arr->setView(z.bars, z.rows, z.center);
  };
  arr.target = [&](bool edge) { return h.arr->benchTarget(edge); };
  arr.clearSelection = [] {};
  arr.selectionInfo = [] { return QString(); };

  ViewOps pr;
  pr.item = h.pr;
  pr.stats = &h.pr->stats();
  pr.setView = [&](const Zoom &z) { h.pr->setView(z.bars, z.rows, z.center); };
  pr.target = [&](bool edge) { return h.pr->benchTarget(edge); };
  pr.empty = [&] { return h.pr->benchEmpty(); };
  pr.clearSelection = [&] { h.pr->clearSelection(); };
  pr.selectionInfo = [&] {
    return QStringLiteral("%1 notes selected").arg(h.pr->selectedCount());
  };

  h.root->setProperty("tab", 0);
  QCoreApplication::processEvents();
  for (int i = 0; i < 3; ++i)
    h.frame({}, nullptr);
  arr.body = h.arr->bodyRect();
  int ci = 0;
  for (const Config &c : arrConfigs) {
    std::printf("\n== ARRANGEMENT, %s ==\n", c.name);
    h.arr->setDrawWaves(c.waves);
    h.arr->setForceRebuild(c.force);
    h.root->setProperty("showHeaders", c.chrome);
    h.root->setProperty("cullDelegates", c.cull);
    QCoreApplication::processEvents();
    printHeader();
    int zi = 0;
    for (const Zoom &z : ARR_ZOOMS) {
      const QString shot =
          ci == 0 ? QStringLiteral("arr_%1").arg(zi) : QString();
      printCase("arrangement", z, runCase(h, arr, z, opt, shot));
      ++zi;
    }
    ++ci;
  }
  h.arr->setDrawWaves(true);
  h.arr->setForceRebuild(false);
  h.root->setProperty("showHeaders", true);
  h.root->setProperty("cullDelegates", false);

  h.root->setProperty("tab", 1);
  QCoreApplication::processEvents();
  for (int i = 0; i < 3; ++i)
    h.frame({}, nullptr);
  pr.body = h.pr->bodyRect();
  ci = 0;
  for (const Config &c : prConfigs) {
    std::printf("\n== PIANO ROLL, %s ==\n", c.name);
    h.pr->setForceRebuild(c.force);
    h.root->setProperty("showHeaders", c.chrome);
    h.root->setProperty("cullDelegates", c.cull);
    QCoreApplication::processEvents();
    printHeader();
    int zi = 0;
    for (const Zoom &z : PR_ZOOMS) {
      const QString shot =
          ci == 0 ? QStringLiteral("pr_%1").arg(zi) : QString();
      printCase("piano roll", z, runCase(h, pr, z, opt, shot));
      ++zi;
    }
    ++ci;
  }
  h.pr->setForceRebuild(false);
  h.root->setProperty("showHeaders", true);
  h.root->setProperty("cullDelegates", false);

  if (opt.naive)
    runNaive(h, opt);
  runChecks(h);
  std::fflush(stdout);
  return 0;
}
