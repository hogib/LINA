// SPDX-License-Identifier: GPL-3.0-or-later
#include "piano_roll_item.hpp"

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
bool isBlack(int pitch) {
  switch (((pitch % 12) + 12) % 12) {
  case 1:
  case 3:
  case 6:
  case 8:
  case 10:
    return true;
  default:
    return false;
  }
}
QString pitchName(int pitch) {
  static const char *const NAMES[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};
  return QString::fromLatin1(NAMES[pitch % 12]) +
         QString::number(pitch / 12 - 1);
}
constexpr Color PLAYHEAD{255, 150, 40, 255};
} // namespace

// Scene graph subtree of this item:
//   root
//   |- rulerClip { rulerBg, rulerXf { rulerTicks, rulerText } }
//   |- bodyClip  { scroll { bg, notes, noteText }, pvXf { pvNotes, pvText },
//   |              band }
//   |- laneClip  { laneBg, laneScroll { stems }, lanePvXf { pvStems } }
//   `- playXf    { playhead }
// Everything under `scroll`, `laneScroll` and `rulerXf` is built for one window
// of the clip and then only moved by matrices until the view leaves that
// window, zooms, or the notes / the selection change.
struct PianoRollItem::Nodes : QSGNode {
  QSGClipNode *rulerClip = sg::makeClipNode();
  QSGClipNode *bodyClip = sg::makeClipNode();
  QSGClipNode *laneClip = sg::makeClipNode();
  QSGGeometryNode *rulerBg = sg::makeGeometryNode(sg::opaqueBackgrounds());
  QSGGeometryNode *rulerTicks = sg::makeGeometryNode();
  QSGGeometryNode *bg = sg::makeGeometryNode(sg::opaqueBackgrounds());
  QSGGeometryNode *notes = sg::makeGeometryNode();
  QSGGeometryNode *pvNotes = sg::makeGeometryNode();
  QSGGeometryNode *band = sg::makeGeometryNode();
  QSGGeometryNode *laneBg = sg::makeGeometryNode(sg::opaqueBackgrounds());
  QSGGeometryNode *stems = sg::makeGeometryNode();
  QSGGeometryNode *pvStems = sg::makeGeometryNode();
  QSGGeometryNode *playhead = sg::makeGeometryNode();
  QSGTransformNode *rulerXf = new QSGTransformNode;
  QSGTransformNode *scroll = new QSGTransformNode;
  QSGTransformNode *pvXf = new QSGTransformNode;
  QSGTransformNode *laneScroll = new QSGTransformNode;
  QSGTransformNode *lanePvXf = new QSGTransformNode;
  QSGTransformNode *playXf = new QSGTransformNode;
  QSGTextNode *rulerText;
  QSGTextNode *noteText;
  QSGTextNode *pvText;
  static constexpr int GEOMETRY_NODES = 10;
  static constexpr int TEXT_NODES = 3;

  explicit Nodes(QQuickWindow *w)
      : rulerText(w->createTextNode()), noteText(w->createTextNode()),
        pvText(w->createTextNode()) {
    rulerText->setColor(QColor(200, 200, 200));
    noteText->setColor(QColor(10, 10, 10));
    pvText->setColor(QColor(10, 10, 10));
    appendChildNode(rulerClip);
    rulerClip->appendChildNode(rulerBg);
    rulerClip->appendChildNode(rulerXf);
    rulerXf->appendChildNode(rulerTicks);
    rulerXf->appendChildNode(rulerText);
    appendChildNode(bodyClip);
    bodyClip->appendChildNode(scroll);
    scroll->appendChildNode(bg);
    scroll->appendChildNode(notes);
    scroll->appendChildNode(noteText);
    bodyClip->appendChildNode(pvXf);
    pvXf->appendChildNode(pvNotes);
    pvXf->appendChildNode(pvText);
    bodyClip->appendChildNode(band);
    appendChildNode(laneClip);
    laneClip->appendChildNode(laneBg);
    laneClip->appendChildNode(laneScroll);
    laneScroll->appendChildNode(stems);
    laneClip->appendChildNode(lanePvXf);
    lanePvXf->appendChildNode(pvStems);
    appendChildNode(playXf);
    playXf->appendChildNode(playhead);
  }
};

PianoRollItem::PianoRollItem(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);
  setAcceptHoverEvents(true);
  setFocus(true);
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(9);
  pitchNames_.setFont(mono);
  QFont sans;
  sans.setPixelSize(11);
  rulerNumbers_.setFont(sans);
}

void PianoRollItem::setController(AppController *c) {
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

void PianoRollItem::setScrollCache(bool on) {
  if (on == scrollCache_)
    return;
  scrollCache_ = on;
  mark(DContent);
  emit optionsChanged();
}

void PianoRollItem::setForceRebuild(bool on) {
  if (on == forceRebuild_)
    return;
  forceRebuild_ = on;
  mark(DAll);
  emit optionsChanged();
}

void PianoRollItem::mark(unsigned flags) {
  dirty_ |= flags;
  update();
}

int PianoRollItem::pitchAt(double y) const {
  return 127 - static_cast<int>(std::floor((y - RULER_H + scrollY_) / keyH_));
}

void PianoRollItem::clampView() {
  if (!ctl_)
    return;
  const QRectF body = bodyRect();
  const double w = std::max(body.width(), 1.0), h = body.height();
  const double len = static_cast<double>(ctl_->project().midi.len) * 1.02;
  ppt_ = std::clamp(ppt_, w / len, MAX_PX_PER_TICK);
  keyH_ = std::clamp(keyH_, h / 128.0, std::max(40.0, h / 128.0));
  scrollTick_ = std::clamp(scrollTick_, 0.0, std::max(len - w / ppt_, 0.0));
  scrollY_ = std::clamp(scrollY_, 0.0, std::max(128.0 * keyH_ - h, 0.0));
}

void PianoRollItem::geometryChange(const QRectF &n, const QRectF &o) {
  QQuickItem::geometryChange(n, o);
  clampView();
  mark(DAll);
  emit viewChanged();
}

std::optional<PianoRollItem::Hit> PianoRollItem::hit(QPointF pos) const {
  if (!ctl_ || !bodyRect().contains(pos))
    return std::nullopt;
  const double t = tickAt(pos.x());
  const auto tt = static_cast<Tick>(std::floor(t));
  const int pitch = pitchAt(pos.y());
  const auto notes = ctl_->project().midi.notesIn(tt, tt + 1);
  for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
    if (it->pitch == pitch && it->start <= tt && tt < it->end()) {
      const double zone =
          std::min(EDGE_PX, static_cast<double>(it->len) * ppt_ / 3.0);
      return Hit{&*it, (static_cast<double>(it->end()) - t) * ppt_ < zone};
    }
  }
  return std::nullopt;
}

std::vector<NoteId> PianoRollItem::selectedIds() const {
  std::vector<NoteId> ids(selected_.begin(), selected_.end());
  std::ranges::sort(ids);
  return ids;
}

void PianoRollItem::clearSelection() {
  if (selected_.empty())
    return;
  selected_.clear();
  mark(DContent);
  emit selectionChanged();
}

// ---------------------------------------------------------------- input

void PianoRollItem::mousePressEvent(QMouseEvent *e) {
  if (!ctl_ || e->button() != Qt::LeftButton) {
    e->ignore();
    return;
  }
  forceActiveFocus(Qt::MouseFocusReason);
  const QPointF pos = e->position();
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  drag_ = {};
  lastPos_ = pos;
  if (pos.y() < RULER_H) {
    drag_.kind = Drag::Scrub;
    ctl_->submit(SetPlayhead{static_cast<Tick>(tickAt(pos.x()))});
  } else if (pos.y() >= laneTop()) {
    drag_.kind = Drag::Velocity;
    updateDrag(pos, e->modifiers());
  } else if (const auto h = hit(pos)) {
    const Note &note = *h->note;
    if (!selected_.contains(note.id)) {
      if (!shift)
        selected_.clear();
      selected_.insert(note.id);
      emit selectionChanged();
    }
    drag_.kind = h->atEdge ? Drag::Resize : Drag::Move;
    drag_.grabTick = tickAt(pos.x());
    drag_.grabStart = note.start;
    drag_.grabEnd = note.end();
    drag_.grabPitch = note.pitch;
    // Selected notes leave the retained content and are drawn from the drag
    // state.
    mark(DContent | DPreview);
  } else {
    drag_.kind = Drag::Band;
    drag_.grabTick = tickAt(pos.x());
    drag_.bandY = pos.y() - RULER_H + scrollY_;
    if (shift)
      drag_.base = selected_;
    updateDrag(pos, e->modifiers());
  }
  e->accept();
}

void PianoRollItem::updateDrag(QPointF pos, Qt::KeyboardModifiers mods) {
  const MidiClip &midi = ctl_->project().midi;
  const double t = tickAt(pos.x());
  const Tick step = gridStep(ppt_, 10.0);
  const auto q = [&](double x) {
    return (mods & Qt::AltModifier) ? static_cast<Tick>(std::round(x))
                                    : snap(x, step);
  };
  lastPos_ = pos;
  switch (drag_.kind) {
  case Drag::Move: {
    const Tick dt =
        q(static_cast<double>(drag_.grabStart) + t - drag_.grabTick) -
        drag_.grabStart;
    const int dpitch = pitchAt(pos.y()) - drag_.grabPitch;
    if (dt != drag_.dt || dpitch != drag_.dpitch) {
      drag_.dt = dt;
      drag_.dpitch = dpitch;
      mark(DPreview);
    }
    break;
  }
  case Drag::Resize: {
    const Tick dlen =
        q(static_cast<double>(drag_.grabEnd) + t - drag_.grabTick) -
        drag_.grabEnd;
    if (dlen != drag_.dlen) {
      drag_.dlen = dlen;
      mark(DPreview);
    }
    break;
  }
  case Drag::Band: {
    const double cy = pos.y() - RULER_H + scrollY_;
    const auto ta = static_cast<Tick>(std::floor(std::min(drag_.grabTick, t)));
    const auto tb = static_cast<Tick>(std::ceil(std::max(drag_.grabTick, t)));
    const int hi =
        127 - static_cast<int>(std::floor(std::min(drag_.bandY, cy) / keyH_));
    const int lo =
        127 - static_cast<int>(std::floor(std::max(drag_.bandY, cy) / keyH_));
    auto sel = drag_.base;
    for (const Note &n : midi.notesIn(ta, tb))
      if (n.end() > ta && n.pitch >= lo && n.pitch <= hi)
        sel.insert(n.id);
    if (sel != selected_) {
      selected_ = std::move(sel);
      mark(DContent);
      emit selectionChanged();
    }
    mark(DBand);
    break;
  }
  case Drag::Velocity: {
    // Paint velocities: notes whose stem is within 4 px of the pointer.
    const double v = (height() - pos.y()) / (VEL_H - 8.0) * 127.0;
    const auto vel = static_cast<std::uint8_t>(std::clamp(v, 1.0, 127.0));
    const auto slack = static_cast<Tick>(4.0 / ppt_);
    const auto tt = static_cast<Tick>(t);
    for (const Note &n : midi.notesIn(tt - slack, tt + slack + 1)) {
      const bool near = std::abs(n.start - tt) <= slack;
      if (near && (selected_.empty() || selected_.contains(n.id)))
        drag_.velocities[n.id] = vel;
    }
    mark(DContent);
    break;
  }
  case Drag::Scrub:
    ctl_->submit(SetPlayhead{static_cast<Tick>(t)});
    break;
  case Drag::None:
    break;
  }
}

void PianoRollItem::mouseMoveEvent(QMouseEvent *e) {
  if (ctl_ && drag_.kind != Drag::None)
    updateDrag(e->position(), e->modifiers());
  e->accept();
}

void PianoRollItem::finishDrag(bool commit) {
  Drag d = std::move(drag_);
  drag_ = {};
  if (d.kind == Drag::None || d.kind == Drag::Scrub)
    return;
  mark(DContent | DPreview | DBand);
  if (!commit || !ctl_)
    return;
  if (d.kind == Drag::Move && (d.dt != 0 || d.dpitch != 0)) {
    ctl_->submit(MoveNotes{selectedIds(), d.dt, d.dpitch});
  } else if (d.kind == Drag::Resize && d.dlen != 0) {
    ctl_->submit(ResizeNotes{selectedIds(), d.dlen});
  } else if (d.kind == Drag::Velocity && !d.velocities.empty()) {
    SetVelocities cmd;
    cmd.changes.assign(d.velocities.begin(), d.velocities.end());
    ctl_->submit(cmd);
  }
}

void PianoRollItem::mouseReleaseEvent(QMouseEvent *e) {
  finishDrag(true);
  e->accept();
}

void PianoRollItem::mouseUngrabEvent() { finishDrag(false); }

void PianoRollItem::hoverMoveEvent(QHoverEvent *e) {
  const auto h = hit(e->position());
  if (h && h->atEdge)
    setCursor(Qt::SizeHorCursor);
  else
    unsetCursor();
}

void PianoRollItem::wheelEvent(QWheelEvent *e) {
  if (!ctl_) {
    e->ignore();
    return;
  }
  QPointF px = e->pixelDelta();
  if (px.isNull())
    px = QPointF(e->angleDelta()) / 120.0 * 48.0;
  const QPointF pos = e->position();
  const double oPpt = ppt_, oTick = scrollTick_, oY = scrollY_, oH = keyH_;
  if (e->modifiers() & Qt::ControlModifier) {
    const double d = px.y() != 0 ? px.y() : px.x();
    const double zoom = std::exp(d / 200.0);
    if (e->modifiers() & Qt::AltModifier) {
      const double bodyY = pos.y() - RULER_H;
      const double anchor = (bodyY + scrollY_) / keyH_;
      keyH_ *= zoom;
      clampView();
      scrollY_ = anchor * keyH_ - bodyY;
    } else {
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
  const bool zoomed = ppt_ != oPpt || keyH_ != oH;
  const bool scrolled = scrollTick_ != oTick || scrollY_ != oY;
  if (zoomed || scrolled) {
    unsigned flags = DView;
    if (zoomed)
      flags |= DContent;
    if (previewing())
      flags |= DPreview;
    if (drag_.kind == Drag::Band)
      flags |= DBand;
    mark(flags);
    emit viewChanged();
  }
  e->accept();
}

void PianoRollItem::keyPressEvent(QKeyEvent *e) {
  if (!ctl_) {
    e->ignore();
    return;
  }
  if (e->key() == Qt::Key_A && (e->modifiers() & Qt::ControlModifier)) {
    selected_.clear();
    for (const Note &n : ctl_->project().midi.notes)
      selected_.insert(n.id);
    mark(DContent);
    emit selectionChanged();
  } else if (e->key() == Qt::Key_Escape) {
    clearSelection();
  } else if (e->key() == Qt::Key_Delete && !selected_.empty()) {
    DeleteNotes cmd{selectedIds()};
    selected_.clear();
    emit selectionChanged();
    ctl_->submit(cmd);
  } else {
    e->ignore();
    return;
  }
  e->accept();
}

// ---------------------------------------------------------------- geometry

void PianoRollItem::emitNote(const Frame &f, const Note &note, Tick start,
                             Tick len, int pitch, bool sel, bool pitchVisible,
                             sg::Mesh &notes, sg::Mesh &stems,
                             std::vector<Label> &labels) {
  const double lx = (static_cast<double>(start) - f.tick) * f.ppt;
  std::uint8_t vel = note.vel;
  if (drag_.kind == Drag::Velocity)
    if (const auto it = drag_.velocities.find(note.id);
        it != drag_.velocities.end())
      vel = it->second;

  // Velocity stem (lane-local coordinates, lane top = 0).
  if (lx >= f.x0 && lx <= f.x1) {
    const auto x = static_cast<float>(lx);
    const auto bottom = static_cast<float>(VEL_H);
    const float y = bottom - static_cast<float>(vel) / 127.0f * (bottom - 8.0f);
    const Color c = sel ? Color{255, 230, 120, 255} : Color{120, 170, 220, 255};
    stems.quad(x, y, x + 1, bottom, c);
    stems.quad(x - 2, y - 2, x + 3, y + 2, c);
  }
  if (!pitchVisible)
    return;

  const double lx1 = (static_cast<double>(start + len) - f.tick) * f.ppt;
  const auto x0 = static_cast<float>(std::max(lx, f.x0));
  const auto x1 = std::max(static_cast<float>(std::min(lx1, f.x1)), x0 + 1);
  const auto top = static_cast<float>((127 - pitch) * f.keyH - f.y);
  const auto keyH = static_cast<float>(f.keyH);
  const float v = static_cast<float>(note.vel) / 127.0f;
  const Color fill =
      sel ? Color{255, static_cast<std::uint8_t>(200 + 55 * v), 120, 255}
          : Color{static_cast<std::uint8_t>(60 + 150 * v),
                  static_cast<std::uint8_t>(150 - 40 * v),
                  static_cast<std::uint8_t>(210 - 150 * v), 255};
  if (x1 - x0 >= 4 && keyH >= 6)
    notes.outlined(x0, top, x1, top + keyH, fill, sel ? gray(255) : gray(15));
  else
    notes.quad(x0, top, x1, top + keyH, fill);
  if (x1 - x0 >= 30 && keyH >= 13)
    labels.push_back({x0 + 3, top + (keyH - 12) * 0.5f, pitch});
}

void PianoRollItem::fillLabels(QSGTextNode *text,
                               const std::vector<Label> &labels) {
  QElapsedTimer t;
  t.start();
  text->clear();
  for (const Label &l : labels) {
    const int pitch = std::clamp(l.pitch, 0, 127);
    QTextLayout *layout = pitchNames_.get(static_cast<std::uint64_t>(pitch),
                                          [&] { return pitchName(pitch); });
    text->addTextLayout(QPointF(l.x, l.y), layout);
  }
  stats_.labelsMs += ms(t);
}

void PianoRollItem::buildContent(Nodes *n, bool overscan) {
  const MidiClip &midi = ctl_->project().midi;
  const QRectF body = bodyRect();
  const double w = body.width(), bodyH = body.height();
  // Overscan only pays when the next frames are scrolls; a zoom or an edit
  // would throw the extra geometry away again, so those rebuilds cover just the
  // visible window.
  const bool cache = overscan && scrollCache_ && !forceRebuild_;
  const double ox = cache ? w * 0.5 : 0.0, oy = cache ? bodyH * 0.5 : 0.0;
  built_ = Frame{scrollTick_, scrollY_, -ox - 4, w + ox + 4, -oy,
                 bodyH + oy,  ppt_,     keyH_,   true};
  const Frame &f = built_;
  notes_.clear();
  stems_.clear();
  labels_.clear();

  const auto t0 = static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt));
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  const int pHi =
      std::min(127 - static_cast<int>(std::floor((f.y + f.y0) / f.keyH)), 127);
  const int pLo =
      std::max(127 - static_cast<int>(std::floor((f.y + f.y1) / f.keyH)), 0);
  const bool preview = previewing();
  const bool anySelected = !selected_.empty();

  QElapsedTimer t;
  t.start();
  int items = 0;
  for (const Note &note : midi.notesIn(t0, t1)) {
    const bool sel = anySelected && selected_.contains(note.id);
    if (note.end() <= t0 || (sel && preview))
      continue;
    const bool vis = note.pitch >= pLo && note.pitch <= pHi;
    emitNote(f, note, note.start, note.len, note.pitch, sel, vis, notes_,
             stems_, labels_);
    items += vis;
  }
  stats_.loopMs += ms(t);
  vContent_ = notes_.commit(n->notes) + stems_.commit(n->stems);
  stats_.vertsRebuilt += vContent_;
  stats_.items = items;
  fillLabels(n->noteText, labels_);
  contentLabels_ = static_cast<int>(labels_.size());
  buildBackground(n);
  buildRuler(n);
  stats_.contentRebuilt = true;
}

void PianoRollItem::buildPreview(Nodes *n) {
  notes_.clear();
  stems_.clear();
  labels_.clear();
  if (previewing()) {
    const MidiClip &midi = ctl_->project().midi;
    const QRectF body = bodyRect();
    const Frame f{scrollTick_, scrollY_,      -4.0, body.width() + 4,
                  0.0,         body.height(), ppt_, keyH_,
                  true};
    const auto t0 = static_cast<Tick>(std::floor(tickAt(0)));
    const auto t1 = static_cast<Tick>(std::ceil(tickAt(width())));
    const int pLo = std::max(pitchAt(body.bottom()), 0);
    const int pHi = std::min(pitchAt(body.top()), 127);
    const Tick dt = drag_.dt, dlen = drag_.dlen;
    // Moved notes are culled against the window shifted back by the gesture
    // offset.
    for (const Note &note :
         midi.notesIn(t0 - dt - std::max<Tick>(dlen, 0), t1 - dt)) {
      if (!selected_.contains(note.id))
        continue;
      const Tick start = note.start + dt;
      const Tick len = std::max(note.len + dlen, MIN_NOTE_LEN);
      const int pitch = note.pitch + drag_.dpitch;
      if (start + len > t0 && start < t1)
        emitNote(f, note, start, len, pitch, true, pitch >= pLo && pitch <= pHi,
                 notes_, stems_, labels_);
    }
  }
  vPreview_ = notes_.commit(n->pvNotes) + stems_.commit(n->pvStems);
  stats_.vertsRebuilt += vPreview_;
  fillLabels(n->pvText, labels_);
  previewLabels_ = static_cast<int>(labels_.size());
}

void PianoRollItem::buildBackground(Nodes *n) {
  // Key rows and grid lines for the built window, in the same local
  // coordinates as the notes, so that scrolling moves them with the same
  // matrix.
  const Frame &f = built_;
  const auto x0 = static_cast<float>(f.x0), x1 = static_cast<float>(f.x1);
  const auto y0 = static_cast<float>(f.y0), y1 = static_cast<float>(f.y1);
  misc_.clear();
  misc_.quad(x0, y0, x1, y1, gray(34));
  const int pHi =
      std::min(127 - static_cast<int>(std::floor((f.y + f.y0) / f.keyH)), 127);
  const int pLo =
      std::max(127 - static_cast<int>(std::floor((f.y + f.y1) / f.keyH)), 0);
  const auto keyH = static_cast<float>(f.keyH);
  for (int p = pLo; p <= pHi; ++p) {
    const auto y = static_cast<float>((127 - p) * f.keyH - f.y);
    if (isBlack(p))
      misc_.quad(x0, y, x1, y + keyH, gray(28));
    if (f.keyH >= 8 || p % 12 == 0)
      misc_.hline(x0, x1, y + keyH, gray(22));
  }
  const Tick step = gridStep(f.ppt, 10.0);
  const auto t0 =
      std::max<Tick>(static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt)), 0);
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  for (Tick g = t0 / step * step; g <= t1; g += step) {
    const std::uint8_t shade = g % BAR == 0 ? 62 : g % (BAR / 4) == 0 ? 46 : 40;
    misc_.vline(static_cast<float>((static_cast<double>(g) - f.tick) * f.ppt),
                y0, y1, gray(shade));
  }
  vBg_ = misc_.commit(n->bg, sg::opaqueBackgrounds());
  stats_.vertsRebuilt += vBg_;
}

void PianoRollItem::buildRuler(Nodes *n) {
  const Frame &f = built_;
  const auto h = static_cast<float>(RULER_H);
  misc_.clear();
  Tick labelStep = 128 * BAR;
  for (const Tick b : {1, 2, 4, 8, 16, 32, 64}) {
    if (static_cast<double>(b * BAR) * f.ppt >= 60.0) {
      labelStep = b * BAR;
      break;
    }
  }
  const auto t0 =
      std::max<Tick>(static_cast<Tick>(std::floor(f.tick + f.x0 / f.ppt)), 0);
  const auto t1 = static_cast<Tick>(std::ceil(f.tick + f.x1 / f.ppt));
  QElapsedTimer t;
  t.start();
  n->rulerText->clear();
  rulerLabels_ = 0;
  for (Tick g = t0 / labelStep * labelStep; g <= t1; g += labelStep) {
    const double x = (static_cast<double>(g) - f.tick) * f.ppt;
    misc_.vline(static_cast<float>(x), 8, h, gray(120));
    const Tick bar = g / BAR + 1;
    QTextLayout *layout = rulerNumbers_.get(
        static_cast<std::uint64_t>(bar), [&] { return QString::number(bar); });
    n->rulerText->addTextLayout(QPointF(x + 4, 3), layout);
    ++rulerLabels_;
  }
  stats_.labelsMs += ms(t);
  vRuler_ = misc_.commit(n->rulerTicks);
  stats_.vertsRebuilt += vRuler_;
}

void PianoRollItem::buildBand(Nodes *n) {
  misc_.clear();
  if (drag_.kind == Drag::Band) {
    const double ax = xOf(drag_.grabTick);
    const double ay = RULER_H + drag_.bandY - scrollY_;
    const auto x0 = static_cast<float>(std::min(ax, lastPos_.x()));
    const auto x1 = static_cast<float>(std::max(ax, lastPos_.x()));
    const auto y0 = static_cast<float>(std::min(ay, lastPos_.y()));
    const auto y1 = static_cast<float>(std::max(ay, lastPos_.y()));
    const Color line{140, 190, 255, 255};
    misc_.quadAlpha(x0, y0, x1, y1, {120, 170, 255, 40});
    misc_.hline(x0, x1, y0, line);
    misc_.hline(x0, x1, y1 - 1, line);
    misc_.vline(x0, y0, y1, line);
    misc_.vline(x1 - 1, y0, y1, line);
  }
  vBand_ = misc_.commit(n->band);
  stats_.vertsRebuilt += vBand_;
}

QSGNode *PianoRollItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
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
  if (!ctl_ || width() <= 0 || height() <= RULER_H + VEL_H)
    return n;
  if (forceRebuild_)
    dirty_ |= DAll;

  const QRectF body = bodyRect();
  const double w = width(), bodyH = body.height();
  sg::setClipRect(n->rulerClip, QRectF(0, 0, w, RULER_H));
  sg::setClipRect(n->bodyClip, body);
  sg::setClipRect(n->laneClip, QRectF(0, laneTop(), w, VEL_H));

  const bool contentDirty = (dirty_ & DContent) || !built_.valid;
  bool scrolledOut = false;
  if (!contentDirty) {
    const double vx = (scrollTick_ - built_.tick) * ppt_;
    const double vy = scrollY_ - built_.y;
    scrolledOut = vx - 4 < built_.x0 || vx + w + 4 > built_.x1 ||
                  vy < built_.y0 || vy + bodyH > built_.y1;
  }
  const bool rebuild = contentDirty || scrolledOut;
  if (rebuild)
    buildContent(n, scrolledOut);
  if (rebuild || (dirty_ & DView)) {
    const double dx = (built_.tick - scrollTick_) * ppt_;
    translate(n->scroll, dx, RULER_H + built_.y - scrollY_);
    translate(n->laneScroll, dx, laneTop());
    translate(n->rulerXf, dx, 0);
  }
  if (dirty_ & DSize) {
    const auto fw = static_cast<float>(w);
    const auto lt = static_cast<float>(laneTop());
    misc_.clear();
    misc_.quad(0, 0, fw, static_cast<float>(RULER_H), gray(40));
    vStatic_ = misc_.commit(n->rulerBg, sg::opaqueBackgrounds());
    misc_.clear();
    misc_.quad(0, lt, fw, static_cast<float>(height()), gray(30));
    misc_.hline(0, fw, lt, gray(70));
    vStatic_ += misc_.commit(n->laneBg, sg::opaqueBackgrounds());
    misc_.clear();
    misc_.quad(0, 0, 1, static_cast<float>(body.bottom()), PLAYHEAD);
    vStatic_ += misc_.commit(n->playhead);
    stats_.vertsRebuilt += vStatic_;
    translate(n->pvXf, 0, RULER_H);
    translate(n->lanePvXf, 0, laneTop());
  }
  if (dirty_ & DPreview)
    buildPreview(n);
  if (dirty_ & DBand)
    buildBand(n);
  if (dirty_ & (DPlayhead | DView)) {
    const double px = xOf(ctl_->project().transport.pos);
    translate(n->playXf, px >= 0 && px <= w ? px : -1.0e6, 0);
  }
  dirty_ = 0;

  stats_.vertsTotal =
      vContent_ + vBg_ + vRuler_ + vPreview_ + vBand_ + vStatic_;
  stats_.geomNodes = Nodes::GEOMETRY_NODES;
  stats_.textNodes = Nodes::TEXT_NODES;
  stats_.labels = contentLabels_ + previewLabels_ + rulerLabels_;
  stats_.upnMs = ms(timer);
  return n;
}

// ---------------------------------------------------------------- bench
// support

void PianoRollItem::setView(double bars, double keys, bool center) {
  const QRectF body = bodyRect();
  ppt_ = body.width() / (bars * static_cast<double>(BAR));
  keyH_ = body.height() / keys;
  scrollTick_ =
      center ? static_cast<double>(ctl_->project().midi.len) * 0.4 : 0.0;
  // Centre vertically on pitch 65, the middle of the generated range.
  scrollY_ = (127.0 - 65.0) * keyH_ - body.height() * 0.5;
  clampView();
  mark(DAll);
  emit viewChanged();
}

std::optional<QPointF> PianoRollItem::benchTarget(bool edge) const {
  const MidiClip &midi = ctl_->project().midi;
  const QRectF body = bodyRect();
  const auto t0 = static_cast<Tick>(std::floor(tickAt(0)));
  const auto t1 = static_cast<Tick>(std::ceil(tickAt(width())));
  const int pLo = std::max(pitchAt(body.bottom()), 0);
  const int pHi = std::min(pitchAt(body.top()), 127);
  const auto margin = static_cast<Tick>(16.0 / ppt_);
  std::vector<const Note *> notes;
  for (const Note &n : midi.notesIn(t0, t1))
    if (n.end() > t0 + margin && n.start < t1 - margin * 20 &&
        n.pitch >= pLo + 2 && n.pitch <= pHi - 2)
      notes.push_back(&n);
  std::ranges::sort(notes, [](const Note *a, const Note *b) {
    return a->len != b->len ? a->len > b->len : a->id < b->id;
  });
  if (notes.size() > 500)
    notes.resize(500);
  for (const Note *n : notes) {
    double x;
    if (edge) {
      if (n->end() >= t1 - margin * 20)
        continue;
      x = xOf(static_cast<double>(n->end())) - 0.5;
    } else {
      const Tick a = std::max(n->start, t0 + margin);
      const Tick b = std::min(n->end(), t1 - margin);
      x = xOf(static_cast<double>(a + b) * 0.5);
    }
    const QPointF pos(x, pitchTop(n->pitch) + keyH_ * 0.5);
    // Another note may be stacked on top; only accept a spot that resolves to
    // this note.
    const auto h = hit(pos);
    if (h && h->note->id == n->id && h->atEdge == edge)
      return pos;
  }
  return std::nullopt;
}

std::optional<QPointF> PianoRollItem::benchEmpty() const {
  const QRectF b = bodyRect();
  for (int i = 1; i < 20; ++i)
    for (int j = 1; j < 20; ++j) {
      const QPointF p(b.left() + b.width() * i / 20.0,
                      b.top() + b.height() * j / 20.0);
      if (!hit(p))
        return p;
    }
  return std::nullopt;
}
