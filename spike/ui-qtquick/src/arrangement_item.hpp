// SPDX-License-Identifier: GPL-3.0-or-later
// Arrangement view: time ruler and clips with waveform overviews, as ONE
// QQuickItem that builds its own scene graph geometry. Track headers are QML
// and follow `scrollY` / `trackHeight`. Scrolling, zooming, culling and hit
// testing are done here against the model; the item never mutates the project,
// it submits `lina::Command`s.
#pragma once

#include "controller.hpp"
#include "sgutil.hpp"

#include <QQuickItem>

#include <optional>

class QSGTextNode;
class QSGTransformNode;

class ArrangementItem : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(AppController *controller READ controller WRITE setController
                 NOTIFY controllerChanged)
  Q_PROPERTY(qreal scrollY READ scrollY NOTIFY viewChanged)
  Q_PROPERTY(qreal trackHeight READ trackHeight NOTIFY viewChanged)
  Q_PROPERTY(qreal rulerHeight READ rulerHeight CONSTANT)
  Q_PROPERTY(
      bool drawWaves MEMBER drawWaves_ WRITE setDrawWaves NOTIFY optionsChanged)
  Q_PROPERTY(bool scrollCache MEMBER scrollCache_ WRITE setScrollCache NOTIFY
                 optionsChanged)
  Q_PROPERTY(bool forceRebuild MEMBER forceRebuild_ WRITE setForceRebuild NOTIFY
                 optionsChanged)

public:
  static constexpr double RULER_H = 28.0;
  static constexpr double EDGE_PX = 6.0;
  static constexpr double MAX_PX_PER_TICK = 1.0;

  explicit ArrangementItem(QQuickItem *parent = nullptr);

  AppController *controller() const { return ctl_; }
  void setController(AppController *c);
  qreal scrollY() const { return scrollY_; }
  qreal trackHeight() const { return trackH_; }
  qreal rulerHeight() const { return RULER_H; }
  /// Bench knob: skip waveforms to separate their cost from the rest.
  void setDrawWaves(bool on);
  /// On: content geometry is built for an overscanned window and scrolled by a
  /// transform node. Off: every scroll step rebuilds the content geometry.
  void setScrollCache(bool on);
  /// Bench knob: rebuild all content geometry in every updatePaintNode call,
  /// which is what an immediate-mode toolkit does.
  void setForceRebuild(bool on);

  // ---- bench support
  void setView(double bars, double rows, bool center);
  std::optional<QPointF> benchTarget(bool edge) const;
  QRectF bodyRect() const {
    return {0, RULER_H, width(), std::max(height() - RULER_H, 1.0)};
  }
  sg::ItemStats &stats() { return stats_; }
  lina::ClipId selected() const { return selected_; }

signals:
  void controllerChanged();
  void viewChanged();
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
    DContent = 1,  ///< clip rects, waveforms, labels
    DView = 2,     ///< background, grid, ruler, scroll transform
    DGhost = 4,    ///< the clip being dragged
    DPlayhead = 8, ///< playhead transform
    DSize = 16,    ///< static parts that depend only on the item size
    DAll = 31,
  };
  struct Drag {
    enum Kind { None, Move, Resize, Scrub } kind = None;
    lina::ClipId id = 0;
    bool leftEdge = false;
    double grab = 0;
    lina::Tick start = 0, len = 0;
    std::size_t track = 0;
    lina::Tick origStart = 0, origLen = 0;
    std::size_t origTrack = 0;
  };
  struct Hit {
    std::size_t track;
    const lina::Clip *clip;
    int edge; ///< 0 none, -1 left, +1 right
  };
  /// Coordinate frame a batch of clips is built in.
  struct Frame {
    double tick = 0, y = 0; ///< content position of local (0, 0)
    double x0 = 0, x1 = 0;  ///< local x range that gets geometry
    double y0 = 0, y1 = 0;  ///< local y range that gets geometry
    double ppt = 0, trackH = 0;
    bool valid = false;
  };
  struct Label {
    float x, y;
    const lina::Clip *clip;
    int chars;
  };
  struct Nodes;

  double xOf(double tick) const { return (tick - scrollTick_) * ppt_; }
  double tickAt(double x) const { return scrollTick_ + x / ppt_; }
  double trackTop(std::size_t t) const {
    return RULER_H + static_cast<double>(t) * trackH_ - scrollY_;
  }
  long trackAt(double y) const;
  void clampView();
  std::optional<Hit> hit(QPointF pos) const;
  void mark(unsigned flags);
  void updateDrag(QPointF pos, Qt::KeyboardModifiers mods);
  void finishDrag(bool commit);

  void buildContent(Nodes *n, bool overscan);
  void buildBackground(Nodes *n);
  void buildRuler(Nodes *n);
  void buildGhost(Nodes *n);
  void drawClip(const Frame &f, const lina::Clip &clip, std::size_t track,
                lina::Tick start, lina::Tick len, lina::Tick offset, bool ghost,
                sg::Mesh &rects, sg::Mesh &waves, std::vector<Label> &labels);
  void fillLabels(QSGTextNode *text, const std::vector<Label> &labels);

  AppController *ctl_ = nullptr;
  double ppt_ = 0.02;
  double scrollTick_ = 0;
  double scrollY_ = 0;
  double trackH_ = 56;
  lina::ClipId selected_ = 0;
  bool drawWaves_ = true;
  bool scrollCache_ = true;
  bool forceRebuild_ = false;
  Drag drag_;
  unsigned dirty_ = DAll;
  Frame built_;
  sg::ItemStats stats_;
  sg::Mesh rects_, waves_, misc_;
  std::vector<Label> labels_;
  sg::LayoutCache clipNames_, rulerNumbers_;
  int contentLabels_ = 0, ghostLabels_ = 0, rulerLabels_ = 0;
  int vContent_ = 0, vBg_ = 0, vRuler_ = 0, vGhost_ = 0, vStatic_ = 0;
};
