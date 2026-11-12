// SPDX-License-Identifier: GPL-3.0-or-later
#include "arrangement_item.hpp"

#include <QCursor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGTextNode>
#include <QSGTransformNode>

#include <algorithm>
#include <cmath>

using namespace lina;
using sg::Color;
using sg::gray;

namespace {
double ms(const QElapsedTimer &t) {
  return static_cast<double>(t.nsecsElapsed()) * 1e-6;
}
void translate(QSGTransformNode *node, double x, double y) {
  QMatrix4x4 m;
  m.translate(static_cast<float>(x), static_cast<float>(y));
  node->setMatrix(m);
  node->markDirty(QSGNode::DirtyMatrix);
}
constexpr Color PLAYHEAD{255, 150, 40, 255};
} // namespace

// Scene graph subtree of this item:
//   root
//   |- rulerClip { rulerBg, rulerXf { rulerTicks, rulerText } }
//   |- bodyClip  { scroll { bg, rects, waves, labels },
//   |              ghostXf { gRects, gWaves, gText } }
//   `- playXf    { playhead }
// Everything under `scroll` and `rulerXf` is built for one window of the
// timeline and then only moved by the two matrices until the view leaves that
// window, zooms, or the document changes.
struct ArrangementItem::Nodes : QSGNode {
  QSGClipNode *rulerClip = sg::makeClipNode();
  QSGClipNode *bodyClip = sg::makeClipNode();
  QSGGeometryNode *rulerBg = sg::makeGeometryNode(sg::opaqueBackgrounds());
  QSGGeometryNode *rulerTicks = sg::makeGeometryNode();
  QSGGeometryNode *bg = sg::makeGeometryNode(sg::opaqueBackgrounds());
  QSGGeometryNode *rects = sg::makeGeometryNode();
  QSGGeometryNode *waves = sg::makeGeometryNode();
  QSGGeometryNode *gRects = sg::makeGeometryNode();
  QSGGeometryNode *gWaves = sg::makeGeometryNode();
  QSGGeometryNode *playhead = sg::makeGeometryNode();
  QSGTransformNode *rulerXf = new QSGTransformNode;
  QSGTransformNode *scroll = new QSGTransformNode;
  QSGTransformNode *ghostXf = new QSGTransformNode;
  QSGTransformNode *playXf = new QSGTransformNode;
  QSGTextNode *rulerText;
  QSGTextNode *labels;
  QSGTextNode *gText;
  static constexpr int GEOMETRY_NODES = 8;
  static constexpr int TEXT_NODES = 3;

  explicit Nodes(QQuickWindow *w)
      : rulerText(w->createTextNode()), labels(w->createTextNode()),
        gText(w->createTextNode()) {
    rulerText->setColor(QColor(200, 200, 200));
    labels->setColor(QColor(235, 235, 235));
    gText->setColor(QColor(235, 235, 235));
    appendChildNode(rulerClip);
    rulerClip->appendChildNode(rulerBg);
    rulerClip->appendChildNode(rulerXf);
    rulerXf->appendChildNode(rulerTicks);
    rulerXf->appendChildNode(rulerText);
    appendChildNode(bodyClip);
    bodyClip->appendChildNode(scroll);
    scroll->appendChildNode(bg);
    scroll->appendChildNode(rects);
    scroll->appendChildNode(waves);
    scroll->appendChildNode(labels);
    bodyClip->appendChildNode(ghostXf);
    ghostXf->appendChildNode(gRects);
    ghostXf->appendChildNode(gWaves);
    ghostXf->appendChildNode(gText);
    appendChildNode(playXf);
    playXf->appendChildNode(playhead);
  }
};

ArrangementItem::ArrangementItem(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);
  setAcceptHoverEvents(true);
  setFocus(true);
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(10);
  clipNames_.setFont(mono);
  QFont sans;
  sans.setPixelSize(11);
  rulerNumbers_.setFont(sans);
}

void ArrangementItem::setController(AppController *c) {
  if (c == ctl_)
    return;
  if (ctl_)
    ctl_->disconnect(this);
  ctl_ = c;
  if (ctl_) {
    connect(ctl_, &AppController::documentChanged, this,
            [this] { mark(DContent); });
    connect(ctl_, &AppController::playheadChanged, this,
            [this] { mark(DPlayhead); });
  }
  clampView();
  mark(DAll);
  emit controllerChanged();
}

void ArrangementItem::setDrawWaves(bool on) {
  if (on == drawWaves_)
    return;
  drawWaves_ = on;
  mark(DContent | DGhost);
  emit optionsChanged();
}

void ArrangementItem::setScrollCache(bool on) {
  if (on == scrollCache_)
    return;
  scrollCache_ = on;
  mark(DContent);
  emit optionsChanged();
}

void ArrangementItem::setForceRebuild(bool on) {
  if (on == forceRebuild_)
    return;
  forceRebuild_ = on;
  mark(DAll);
  emit optionsChanged();
}

void ArrangementItem::mark(unsigned flags) {
  dirty_ |= flags;
  update();
}

long ArrangementItem::trackAt(double y) const {
  return static_cast<long>(std::floor((y - RULER_H + scrollY_) / trackH_));
}

void ArrangementItem::clampView() {
  if (!ctl_)
    return;
  const Project &p = ctl_->project();
  const double w = std::max(width(), 1.0);
  const double h = std::max(height() - RULER_H, 1.0);
  const double len = static_cast<double>(p.len) * 1.02;
  ppt_ = std::clamp(ppt_, w / len, MAX_PX_PER_TICK);
  const auto tracks = static_cast<double>(p.tracks.size());
  trackH_ = std::clamp(trackH_, h / tracks, std::max(160.0, h / tracks));
  scrollTick_ = std::clamp(scrollTick_, 0.0, std::max(len - w / ppt_, 0.0));
  scrollY_ = std::clamp(scrollY_, 0.0, std::max(tracks * trackH_ - h, 0.0));
}

void ArrangementItem::geometryChange(const QRectF &n, const QRectF &o) {
  QQuickItem::geometryChange(n, o);
  clampView();
  mark(DAll);
  emit viewChanged();
}

std::optional<ArrangementItem::Hit> ArrangementItem::hit(QPointF pos) const {
  if (!ctl_ || !bodyRect().contains(pos))
    return std::nullopt;
  const Project &p = ctl_->project();
  const long ti = trackAt(pos.y());
  if (ti < 0 || ti >= static_cast<long>(p.tracks.size()))
    return std::nullopt;
  const double t = tickAt(pos.x());
  const auto tt = static_cast<Tick>(std::floor(t));
  const auto clips = p.tracks[static_cast<std::size_t>(ti)].clipsIn(tt, tt + 1);
  for (auto it = clips.rbegin(); it != clips.rend(); ++it) {
    if (it->start <= tt && tt < it->end()) {
      const double zone =
          std::min(EDGE_PX, static_cast<double>(it->len) * ppt_ / 4.0);
      int edge = 0;
      if ((t - static_cast<double>(it->start)) * ppt_ < zone)
        edge = -1;
      else if ((static_cast<double>(it->end()) - t) * ppt_ < zone)
        edge = 1;
      return Hit{static_cast<std::size_t>(ti), &*it, edge};
    }
  }
  return std::nullopt;
}

// ---------------------------------------------------------------- input

void ArrangementItem::mousePressEvent(QMouseEvent *e) {
  if (!ctl_ || e->button() != Qt::LeftButton) {
    e->ignore();
    return;
  }
  forceActiveFocus(Qt::MouseFocusReason);
  const QPointF pos = e->position();
  if (pos.y() < RULER_H) {
    drag_ = {};
    drag_.kind = Drag::Scrub;
    ctl_->submit(SetPlayhead{static_cast<Tick>(tickAt(pos.x()))});
  } else if (const auto h = hit(pos)) {
    const Clip &c = *h->clip;
    selected_ = c.id;
    drag_ = {};
    drag_.kind = h->edge == 0 ? Drag::Move : Drag::Resize;
    drag_.id = c.id;
    drag_.leftEdge = h->edge < 0;
    drag_.grab = tickAt(pos.x()) - static_cast<double>(c.start);
    drag_.start = drag_.origStart = c.start;
    drag_.len = drag_.origLen = c.len;
    drag_.track = drag_.origTrack = h->track;
    // The dragged clip leaves the retained content and is drawn from the drag
    // state.
    mark(DContent | DGhost);
  } else if (selected_ != 0) {
    selected_ = 0;
    mark(DContent);
  }
  e->accept();
}

void ArrangementItem::updateDrag(QPointF pos, Qt::KeyboardModifiers mods) {
  const Project &p = ctl_->project();
  const double t = tickAt(pos.x());
  const Tick step = gridStep(ppt_, 12.0);
  const auto q = [&](double x) {
    return (mods & Qt::ShiftModifier) ? static_cast<Tick>(std::round(x))
                                      : snap(x, step);
  };
  const Drag before = drag_;
  switch (drag_.kind) {
  case Drag::Move:
    drag_.start = std::max<Tick>(q(t - drag_.grab), 0);
    drag_.track = static_cast<std::size_t>(std::clamp<long>(
        trackAt(pos.y()), 0, static_cast<long>(p.tracks.size()) - 1));
    break;
  case Drag::Resize:
    if (drag_.leftEdge) {
      const Tick end = drag_.origStart + drag_.origLen;
      drag_.start = std::clamp<Tick>(q(t), 0, end - MIN_CLIP_LEN);
      drag_.len = end - drag_.start;
    } else {
      drag_.len = std::max(q(t) - drag_.start, MIN_CLIP_LEN);
    }
    break;
  case Drag::Scrub:
    ctl_->submit(SetPlayhead{static_cast<Tick>(t)});
    break;
  case Drag::None:
    break;
  }
  // Snapping makes most pointer moves a no-op: nothing is marked dirty then.
  if (drag_.start != before.start || drag_.len != before.len ||
      drag_.track != before.track)
    mark(DGhost);
}

void ArrangementItem::mouseMoveEvent(QMouseEvent *e) {
  if (ctl_ && drag_.kind != Drag::None)
    updateDrag(e->position(), e->modifiers());
  e->accept();
}

void ArrangementItem::finishDrag(bool commit) {
  const Drag d = drag_;
  drag_ = {};
  if (d.kind == Drag::Move || d.kind == Drag::Resize)
    mark(DContent | DGhost);
  if (!commit || !ctl_)
    return;
  if (d.kind == Drag::Move &&
      (d.start != d.origStart || d.track != d.origTrack))
    ctl_->submit(MoveClip{d.id, d.track, d.start});
  else if (d.kind == Drag::Resize &&
           (d.start != d.origStart || d.len != d.origLen))
    ctl_->submit(ResizeClip{d.id, d.start, d.len});
}

void ArrangementItem::mouseReleaseEvent(QMouseEvent *e) {
  finishDrag(true);
  e->accept();
}

void ArrangementItem::mouseUngrabEvent() { finishDrag(false); }

void ArrangementItem::hoverMoveEvent(QHoverEvent *e) {
  const auto h = hit(e->position());
  if (h && h->edge != 0)
    setCursor(Qt::SizeHorCursor);
  else
    unsetCursor();
}

void ArrangementItem::wheelEvent(QWheelEvent *e) {
  if (!ctl_) {
    e->ignore();
    return;
  }
  QPointF px = e->pixelDelta();
  if (px.isNull())
    px = QPointF(e->angleDelta()) / 120.0 * 48.0;
  const QPointF pos = e->position();
  const double oPpt = ppt_, oTick = scrollTick_, oY = scrollY_, oH = trackH_;
  if (e->modifiers() & Qt::ControlModifier) {
    // Alt makes some platforms swap the wheel axes, so take whichever is set.
    const double d = px.y() != 0 ? px.y() : px.x();
    const double zoom = std::exp(d / 200.0);
    if (e->modifiers() & Qt::AltModifier) {
      // ctrl+alt+wheel: track height, anchored at the cursor.
      const double bodyY = pos.y() - RULER_H;
      const double anchor = (bodyY + scrollY_) / trackH_;
      trackH_ *= zoom;
      clampView();
      scrollY_ = anchor * trackH_ - bodyY;
    } else {
      // ctrl+wheel: time zoom, anchored at the cursor.
      const double anchor = tickAt(pos.x());
      ppt_ *= zoom;
      clampView();
      scrollTick_ = anchor - pos.x() / ppt_;
    }
  } else {
    scrollTick_ -= px.x() / ppt_;
    scrollY_ -= px.y();
  }
  clampView();
  const bool zoomed = ppt_ != oPpt || trackH_ != oH;
  const bool scrolled = scrollTick_ != oTick || scrollY_ != oY;
  if (zoomed || scrolled) {
    unsigned flags = DView;
    if (zoomed)
      flags |= DContent;
    if (drag_.kind == Drag::Move || drag_.kind == Drag::Resize)
      flags |= DGhost;
    mark(flags);
    emit viewChanged();
  }
  e->accept();
}

void ArrangementItem::keyPressEvent(QKeyEvent *e) {
  if (ctl_ && e->key() == Qt::Key_Delete && selected_ != 0) {
    const ClipId id = selected_;
    selected_ = 0;
    ctl_->submit(DeleteClip{id});
    e->accept();
    return;
  }
  e->ignore();
}

// ---------------------------------------------------------------- geometry

void ArrangementItem::drawClip(const Frame &f, const Clip &clip,
                               std::size_t track, Tick start, Tick len,
                               Tick offset, bool ghost, sg::Mesh &rects,
                               sg::Mesh &waves, std::vector<Label> &labels) {
  const Project &p = ctl_->project();
  const Track &tr = p.tracks[track];
  const double x0 = (static_cast<double>(start) - f.tick) * f.ppt;
  const double x1 = (static_cast<double>(start + len) - f.tick) * f.ppt;
  const double top = static_cast<double>(track) * f.trackH - f.y;
  // Clamp so that huge off-screen coordinates never reach the vertex buffer.
  const auto rx0 = static_cast<float>(std::max(x0, f.x0));
  const auto rx1 = std::max(static_cast<float>(std::min(x1, f.x1)), rx0 + 1);
  const auto ry0 = static_cast<float>(top + 1);
  const auto ry1 = static_cast<float>(top + f.trackH - 1);
  const float sat = tr.mute ? 0.08f : 0.5f;
  const Color fill = sg::hsv(tr.hue, sat, ghost ? 0.42f : 0.5f);
  const float w = rx1 - rx0;
  if (selected_ == clip.id)
    rects.outlined(rx0, ry0, rx1, ry1, fill, gray(255));
  else
    rects.quad(rx0, ry0, rx1, ry1, fill);

  if (drawWaves_ && w >= 3 && f.trackH >= 8) {
    // Min/max envelope, one column per pixel, from the mipmap level matching
    // the zoom.
    const float wx0 = std::floor(rx0);
    if (rx1 - wx0 >= 2) {
      const AudioSource &src = p.sources[clip.source];
      const double spp = SAMPLES_PER_TICK / f.ppt;
      const int level = src.levelFor(spp);
      const float mid = (ry0 + ry1) * 0.5f;
      const float half = (ry1 - ry0) * 0.5f - 1;
      const Color col = sg::hsv(tr.hue, sat * 0.4f, 0.95f);
      const double base =
          f.tick - static_cast<double>(start) + static_cast<double>(offset);
      const quint32 strip = waves.stripBegin();
      for (float x = wx0; x <= rx1; x += 1) {
        const double s0 = (base + x / f.ppt) * SAMPLES_PER_TICK;
        const Peak pk = src.peak(level, s0, s0 + spp);
        const float ya = mid - pk.hi * half;
        const float yb = std::max(mid - pk.lo * half, ya + 1);
        waves.stripColumn(x, ya, yb, col);
      }
      waves.stripEnd(strip);
    }
  }
  if (w >= 34 && f.trackH >= 26) {
    const int chars = std::min(static_cast<int>((w - 8) / 6),
                               static_cast<int>(clip.name.size()));
    labels.push_back({rx0 + 4, ry0 + 2, &clip, chars});
  }
}

void ArrangementItem::fillLabels(QSGTextNode *text,
                                 const std::vector<Label> &labels) {
  QElapsedTimer t;
  t.start();
  // One text node carries every label. Labels are cut by character count like
  // in the egui spike; a clip node per label would give exact clipping but one
  // batch per label.
  text->clear();
  for (const Label &l : labels) {
    const std::uint64_t key = (static_cast<std::uint64_t>(l.clip->id) << 8) |
                              static_cast<std::uint64_t>(l.chars);
    QTextLayout *layout = clipNames_.get(
        key, [&] { return QString::fromUtf8(l.clip->name.data(), l.chars); });
    text->addTextLayout(QPointF(l.x, l.y), layout);
  }
  stats_.labelsMs += ms(t);
}

void ArrangementItem::buildContent(Nodes *n, bool overscan) {
  const Project &p = ctl_->project();
  const double w = width(), bodyH = height() - RULER_H;
  // Overscan only pays when the next frames are scrolls; a zoom or an edit
  // would throw the extra geometry away again, so those rebuilds cover just the
  // visible window.
  const bool cache = overscan && scrollCache_ && !forceRebuild_;
  const double ox = cache ? w * 0.5 : 0.0, oy = cache ? bodyH * 0.5 : 0.0;
  built_ = Frame{scrollTick_, scrollY_, -ox - 4, w + ox + 4, -oy,
                 bodyH + oy,  ppt_,     trackH_, true};
  const Frame &f = built_;
  rects_.clear();
  waves_.clear();
  labels_.clear();
  if (clipNames_.size() > 60000)
    clipNames_.clear();

  const auto t0 = static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt));
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  const auto nTracks = static_cast<double>(p.tracks.size());
  const auto first = static_cast<std::size_t>(
      std::clamp(std::floor((f.y + f.y0) / f.trackH), 0.0, nTracks));
  const auto last = static_cast<std::size_t>(
      std::clamp(std::ceil((f.y + f.y1) / f.trackH), 0.0, nTracks));
  const ClipId dragged =
      drag_.kind == Drag::Move || drag_.kind == Drag::Resize ? drag_.id : 0;

  QElapsedTimer t;
  t.start();
  int items = 0;
  for (std::size_t ti = first; ti < last; ++ti) {
    for (const Clip &c : p.tracks[ti].clipsIn(t0, t1)) {
      if (c.end() <= t0 || c.id == dragged)
        continue;
      drawClip(f, c, ti, c.start, c.len, c.offset, false, rects_, waves_,
               labels_);
      ++items;
    }
  }
  stats_.loopMs += ms(t);
  vContent_ = rects_.commit(n->rects) + waves_.commit(n->waves);
  stats_.vertsRebuilt += vContent_;
  stats_.items = items;
  stats_.waveCols = static_cast<int>(waves_.vertexCount() / 2);
  fillLabels(n->labels, labels_);
  contentLabels_ = static_cast<int>(labels_.size());
  buildBackground(n);
  buildRuler(n);
  stats_.contentRebuilt = true;
}

void ArrangementItem::buildBackground(Nodes *n) {
  // Rows and grid lines for the built window, in the same local coordinates as
  // the clips, so that scrolling moves them with the same matrix.
  const Project &p = ctl_->project();
  const Frame &f = built_;
  const auto x0 = static_cast<float>(f.x0), x1 = static_cast<float>(f.x1);
  const auto y0 = static_cast<float>(f.y0), y1 = static_cast<float>(f.y1);
  misc_.clear();
  misc_.quad(x0, y0, x1, y1, gray(26));
  const auto nTracks = static_cast<double>(p.tracks.size());
  const auto first = static_cast<long>(
      std::clamp(std::floor((f.y + f.y0) / f.trackH), 0.0, nTracks));
  const auto last = static_cast<long>(
      std::clamp(std::ceil((f.y + f.y1) / f.trackH), 0.0, nTracks));
  for (long ti = first; ti < last; ++ti) {
    const auto y = static_cast<float>(static_cast<double>(ti) * f.trackH - f.y);
    if (ti % 2 == 1)
      misc_.quad(x0, y, x1, y + static_cast<float>(f.trackH), gray(30));
    misc_.hline(x0, x1, y, gray(18));
  }
  const Tick step = gridStep(f.ppt, 12.0);
  const auto t0 =
      std::max<Tick>(static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt)), 0);
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  for (Tick g = t0 / step * step; g <= t1; g += step) {
    const std::uint8_t shade = g % (BAR * 4) == 0 ? 58 : g % BAR == 0 ? 46 : 36;
    misc_.vline(static_cast<float>((static_cast<double>(g) - f.tick) * f.ppt),
                y0, y1, gray(shade));
  }
  vBg_ = misc_.commit(n->bg, sg::opaqueBackgrounds());
  stats_.vertsRebuilt += vBg_;
}

void ArrangementItem::buildRuler(Nodes *n) {
  const Frame &f = built_;
  const auto h = static_cast<float>(RULER_H);
  const auto lx = [&](Tick g) {
    return (static_cast<double>(g) - f.tick) * f.ppt;
  };
  misc_.clear();
  const auto t0 =
      std::max<Tick>(static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt)), 0);
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  const Tick step = gridStep(f.ppt, 12.0);
  for (Tick g = t0 / step * step; g <= t1; g += step) {
    const float len = g % BAR == 0 ? 10.0f : 5.0f;
    misc_.vline(static_cast<float>(lx(g)), h - len, h, gray(120));
  }
  vRuler_ = misc_.commit(n->rulerTicks);
  stats_.vertsRebuilt += vRuler_;

  Tick labelStep = 256 * BAR;
  for (const Tick b : {1, 2, 4, 8, 16, 32, 64, 128}) {
    if (static_cast<double>(b * BAR) * f.ppt >= 70.0) {
      labelStep = b * BAR;
      break;
    }
  }
  QElapsedTimer t;
  t.start();
  n->rulerText->clear();
  rulerLabels_ = 0;
  for (Tick g = t0 / labelStep * labelStep; g <= t1; g += labelStep) {
    const Tick bar = g / BAR + 1;
    QTextLayout *layout = rulerNumbers_.get(
        static_cast<std::uint64_t>(bar), [&] { return QString::number(bar); });
    n->rulerText->addTextLayout(QPointF(lx(g) + 4, 4), layout);
    ++rulerLabels_;
  }
  stats_.labelsMs += ms(t);
}

void ArrangementItem::buildGhost(Nodes *n) {
  rects_.clear();
  waves_.clear();
  labels_.clear();
  if (drag_.kind == Drag::Move || drag_.kind == Drag::Resize) {
    if (const auto ref = ctl_->project().findClip(drag_.id)) {
      const Clip &c = *ref->clip;
      const bool resize = drag_.kind == Drag::Resize;
      const std::size_t track = resize ? ref->track : drag_.track;
      const Tick len = resize ? drag_.len : c.len;
      const Tick offset = c.offset + (resize ? drag_.start - c.start : 0);
      const Frame f{scrollTick_, scrollY_, -4.0,    width() + 4, 0.0,
                    height(),    ppt_,     trackH_, true};
      drawClip(f, c, track, drag_.start, len, offset, true, rects_, waves_,
               labels_);
    }
  }
  vGhost_ = rects_.commit(n->gRects) + waves_.commit(n->gWaves);
  stats_.vertsRebuilt += vGhost_;
  fillLabels(n->gText, labels_);
  ghostLabels_ = static_cast<int>(labels_.size());
}

QSGNode *ArrangementItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  QElapsedTimer timer;
  timer.start();
  ++stats_.calls;
  stats_.upnMs = stats_.loopMs = stats_.labelsMs = 0;
  stats_.vertsRebuilt = 0;
  stats_.contentRebuilt = false;

  auto *n = static_cast<Nodes *>(old);
  if (!n) {
    n = new Nodes(window());
    dirty_ = DAll;
    built_.valid = false;
  }
  if (!ctl_ || width() <= 0 || height() <= RULER_H)
    return n;
  if (forceRebuild_)
    dirty_ |= DAll;

  const double w = width(), bodyH = height() - RULER_H;
  sg::setClipRect(n->rulerClip, QRectF(0, 0, w, RULER_H));
  sg::setClipRect(n->bodyClip, QRectF(0, RULER_H, w, bodyH));

  const bool contentDirty = (dirty_ & DContent) || !built_.valid;
  bool scrolledOut = false;
  if (!contentDirty) {
    // Is the visible window still inside the window the content was built for?
    const double vx = (scrollTick_ - built_.tick) * ppt_;
    const double vy = scrollY_ - built_.y;
    scrolledOut = vx - 4 < built_.x0 || vx + w + 4 > built_.x1 ||
                  vy < built_.y0 || vy + bodyH > built_.y1;
  }
  const bool rebuild = contentDirty || scrolledOut;
  if (rebuild)
    buildContent(n, scrolledOut);
  if (rebuild || (dirty_ & DView)) {
    // A scroll frame that stays inside the built window ends here: two
    // matrices.
    const double dx = (built_.tick - scrollTick_) * ppt_;
    translate(n->scroll, dx, RULER_H + built_.y - scrollY_);
    translate(n->rulerXf, dx, 0);
  }
  if (dirty_ & DSize) {
    misc_.clear();
    misc_.quad(0, 0, static_cast<float>(w), static_cast<float>(RULER_H),
               gray(40));
    vStatic_ = misc_.commit(n->rulerBg, sg::opaqueBackgrounds());
    misc_.clear();
    misc_.quad(0, 0, 1, static_cast<float>(height()), PLAYHEAD);
    vStatic_ += misc_.commit(n->playhead);
    stats_.vertsRebuilt += vStatic_;
    translate(n->ghostXf, 0, RULER_H);
  }
  if (dirty_ & DGhost)
    buildGhost(n);
  if (dirty_ & (DPlayhead | DView)) {
    // Playhead-only frames end up here and nowhere else: one matrix.
    const double px = xOf(ctl_->project().transport.pos);
    translate(n->playXf, px >= 0 && px <= w ? px : -1.0e6, 0);
  }
  dirty_ = 0;

  stats_.vertsTotal = vContent_ + vBg_ + vRuler_ + vGhost_ + vStatic_;
  stats_.geomNodes = Nodes::GEOMETRY_NODES;
  stats_.textNodes = Nodes::TEXT_NODES;
  stats_.labels = contentLabels_ + ghostLabels_ + rulerLabels_;
  stats_.upnMs = ms(timer);
  return n;
}

// ---------------------------------------------------------------- bench
// support

void ArrangementItem::setView(double bars, double rows, bool center) {
  const Project &p = ctl_->project();
  const QRectF body = bodyRect();
  ppt_ = body.width() / (bars * static_cast<double>(BAR));
  trackH_ = body.height() / rows;
  if (center) {
    scrollTick_ = static_cast<double>(p.len) * 0.4;
    scrollY_ =
        (static_cast<double>(p.tracks.size()) * trackH_ - body.height()) * 0.4;
  } else {
    scrollTick_ = 0;
    scrollY_ = 0;
  }
  clampView();
  mark(DAll);
  emit viewChanged();
}

std::optional<QPointF> ArrangementItem::benchTarget(bool edge) const {
  const Project &p = ctl_->project();
  const QRectF body = bodyRect();
  const auto t0 = static_cast<Tick>(std::floor(tickAt(0)));
  const auto t1 = static_cast<Tick>(std::ceil(tickAt(width())));
  const long first = std::max(trackAt(body.top()), 0L);
  const long last =
      std::min(trackAt(body.bottom()) + 1, static_cast<long>(p.tracks.size()));
  const long mid = (first + last) / 2;
  const auto margin = static_cast<Tick>(16.0 / ppt_);
  std::vector<long> order;
  for (long t = mid; t < last; ++t)
    order.push_back(t);
  for (long t = first; t < mid; ++t)
    order.push_back(t);
  for (const long ti : order) {
    const double y = trackTop(static_cast<std::size_t>(ti)) + trackH_ * 0.5;
    if (y < body.top() + 4 || y > body.bottom() - 4)
      continue;
    std::vector<const Clip *> clips;
    for (const Clip &c : p.tracks[static_cast<std::size_t>(ti)].clipsIn(t0, t1))
      if (c.end() > t0)
        clips.push_back(&c);
    std::ranges::stable_sort(
        clips, [](const Clip *a, const Clip *b) { return a->len > b->len; });
    for (const Clip *c : clips) {
      double x;
      if (edge) {
        if (c->end() >= t1 - margin * 20)
          continue; // leave room to drag right
        x = xOf(static_cast<double>(c->end())) - 1.0;
      } else {
        const Tick a = std::max(c->start, t0 + margin);
        const Tick b = std::min(c->end(), t1 - margin);
        x = xOf(static_cast<double>(a + b) * 0.5);
      }
      const QPointF pos(x, y);
      const auto h = hit(pos);
      if (h && h->clip->id == c->id && (h->edge != 0) == edge)
        return pos;
    }
  }
  return std::nullopt;
}
