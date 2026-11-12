// SPDX-License-Identifier: GPL-3.0-or-later
// Piano roll: ruler, note grid and velocity lane as ONE QQuickItem with its own
// scene graph geometry. The keyboard column is QML and follows `scrollY` /
// `keyHeight`. Same structure as the arrangement item: own
// scroll/zoom/cull/hit-test, gesture state kept in the view, `lina::Command`s
// submitted when a gesture ends.
#pragma once

#include "controller.hpp"
#include "sgutil.hpp"

#include <QQuickItem>

#include <optional>
#include <unordered_map>
#include <unordered_set>

class QSGTextNode;
class QSGTransformNode;

class PianoRollItem : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(AppController *controller READ controller WRITE setController
                 NOTIFY controllerChanged)
  Q_PROPERTY(qreal scrollY READ scrollY NOTIFY viewChanged)
  Q_PROPERTY(qreal keyHeight READ keyHeight NOTIFY viewChanged)
  Q_PROPERTY(qreal rulerHeight READ rulerHeight CONSTANT)
  Q_PROPERTY(qreal laneHeight READ laneHeight CONSTANT)
  Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
  Q_PROPERTY(bool scrollCache MEMBER scrollCache_ WRITE setScrollCache NOTIFY
                 optionsChanged)
  Q_PROPERTY(bool forceRebuild MEMBER forceRebuild_ WRITE setForceRebuild NOTIFY
                 optionsChanged)

public:
  static constexpr double RULER_H = 22.0;
  static constexpr double VEL_H = 120.0;
  static constexpr double EDGE_PX = 6.0;
  static constexpr double MAX_PX_PER_TICK = 1.0;

  explicit PianoRollItem(QQuickItem *parent = nullptr);

  AppController *controller() const { return ctl_; }
  void setController(AppController *c);
  qreal scrollY() const { return scrollY_; }
  qreal keyHeight() const { return keyH_; }
  qreal rulerHeight() const { return RULER_H; }
  qreal laneHeight() const { return VEL_H; }
  int selectedCount() const { return static_cast<int>(selected_.size()); }
  void setScrollCache(bool on);
  void setForceRebuild(bool on);

  // ---- bench support
  void setView(double bars, double keys, bool center);
  std::optional<QPointF> benchTarget(bool edge) const;
  std::optional<QPointF> benchEmpty() const;
  void clearSelection();
  QRectF bodyRect() const {
    return {0, RULER_H, width(), std::max(height() - RULER_H - VEL_H, 1.0)};
  }
  sg::ItemStats &stats() { return stats_; }

signals:
  void controllerChanged();
  void viewChanged();
  void selectionChanged();
  void optionsChanged();

protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &n, const QRectF &o) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseUngrabEvent() override;
  void hoverMoveEvent(QHoverEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;

private:
  enum Dirty : unsigned {
    DContent = 1, ///< notes, note labels, velocity stems
    DView = 2,    ///< background, grid, ruler, scroll transforms
    DPreview = 4, ///< selected notes while a move/resize is in flight
    DBand = 8,    ///< rubber band rectangle
    DPlayhead = 16,
    DSize = 32, ///< static parts that depend only on the item size
    DAll = 63,
  };
  struct Drag {
    enum Kind { None, Band, Move, Resize, Velocity, Scrub } kind = None;
    double grabTick = 0;
    double bandY =
        0; ///< band anchor in content coordinates, survives scrolling
    lina::Tick grabStart = 0, grabEnd = 0;
    int grabPitch = 0;
    lina::Tick dt = 0, dlen = 0;
    int dpitch = 0;
    std::unordered_set<lina::NoteId> base;
    std::unordered_map<lina::NoteId, std::uint8_t> velocities;
  };
  struct Hit {
    const lina::Note *note;
    bool atEdge;
  };
  struct Frame {
    double tick = 0, y = 0;
    double x0 = 0, x1 = 0;
    double y0 = 0, y1 = 0;
    double ppt = 0, keyH = 0;
    bool valid = false;
  };
  struct Label {
    float x, y;
    int pitch;
  };
  struct Nodes;

  double xOf(double tick) const { return (tick - scrollTick_) * ppt_; }
  double tickAt(double x) const { return scrollTick_ + x / ppt_; }
  double pitchTop(int pitch) const {
    return RULER_H + (127 - pitch) * keyH_ - scrollY_;
  }
  int pitchAt(double y) const;
  double laneTop() const { return height() - VEL_H; }
  bool previewing() const {
    return drag_.kind == Drag::Move || drag_.kind == Drag::Resize;
  }
  void clampView();
  std::optional<Hit> hit(QPointF pos) const;
  void mark(unsigned flags);
  void updateDrag(QPointF pos, Qt::KeyboardModifiers mods);
  void finishDrag(bool commit);
  std::vector<lina::NoteId> selectedIds() const;

  void buildContent(Nodes *n, bool overscan);
  void buildPreview(Nodes *n);
  void buildBackground(Nodes *n);
  void buildRuler(Nodes *n);
  void buildBand(Nodes *n);
  void emitNote(const Frame &f, const lina::Note &note, lina::Tick start,
                lina::Tick len, int pitch, bool sel, bool pitchVisible,
                sg::Mesh &notes, sg::Mesh &stems, std::vector<Label> &labels);
  void fillLabels(QSGTextNode *text, const std::vector<Label> &labels);

  AppController *ctl_ = nullptr;
  double ppt_ = 0.05;
  double scrollTick_ = 0;
  double scrollY_ = 16.0 * 40.0;
  double keyH_ = 16;
  std::unordered_set<lina::NoteId> selected_;
  bool scrollCache_ = true;
  bool forceRebuild_ = false;
  Drag drag_;
  QPointF lastPos_;
  unsigned dirty_ = DAll;
  Frame built_;
  sg::ItemStats stats_;
  sg::Mesh notes_, stems_, misc_;
  std::vector<Label> labels_;
  sg::LayoutCache pitchNames_, rulerNumbers_;
  int contentLabels_ = 0, previewLabels_ = 0, rulerLabels_ = 0;
  int vContent_ = 0, vBg_ = 0, vRuler_ = 0, vPreview_ = 0, vBand_ = 3,
      vStatic_ = 0;
};
