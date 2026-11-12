// SPDX-License-Identifier: GPL-3.0-or-later
// Small helpers shared by the two custom scene graph items: a quad mesh builder
// that commits into a QSGGeometryNode, colours, a text layout cache and
// per-frame statistics.
#pragma once

#include <QFont>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QString>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sg {

struct Color {
  std::uint8_t r, g, b, a = 255;
};

constexpr Color gray(std::uint8_t v) { return {v, v, v, 255}; }

inline Color hsv(float h, float s, float v) {
  const float hh = (h - std::floor(h)) * 6.0f;
  const int i = static_cast<int>(hh);
  const float f = hh - static_cast<float>(i);
  const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float r, g, b;
  switch (i % 6) {
  case 0:
    r = v, g = t, b = p;
    break;
  case 1:
    r = q, g = v, b = p;
    break;
  case 2:
    r = p, g = v, b = t;
    break;
  case 3:
    r = p, g = q, b = v;
    break;
  case 4:
    r = t, g = p, b = v;
    break;
  default:
    r = v, g = p, b = q;
    break;
  }
  const auto u8 = [](float x) {
    return static_cast<std::uint8_t>(std::lround(x * 255.0f));
  };
  return {u8(r), u8(g), u8(b), 255};
}

/// What one updatePaintNode call did. Reset by the item at the start of each
/// call; the bench zeroes `calls` before a frame to detect frames in which the
/// item was not asked.
struct ItemStats {
  int calls = 0;
  double upnMs = 0;     ///< whole updatePaintNode
  double loopMs = 0;    ///< clip/note loop inside it (quads + waveform columns)
  double labelsMs = 0;  ///< text layout + QSGTextNode fill inside it
  int vertsRebuilt = 0; ///< vertices written this call
  int vertsTotal = 0;   ///< vertices held by this item's geometry nodes
  int geomNodes =
      0; ///< QSGGeometryNodes owned by this item (text nodes excluded)
  int textNodes = 0;
  int labels = 0;   ///< text layouts currently in the text nodes
  int items = 0;    ///< clips / notes in the retained content geometry
  int waveCols = 0; ///< waveform columns in the retained content geometry
  bool contentRebuilt = false;
};

/// Indexed triangle list of coloured quads. The vectors are reused between
/// frames.
class Mesh {
public:
  void clear() {
    v_.clear();
    i_.clear();
  }
  std::size_t vertexCount() const { return v_.size(); }

  void quad(float x0, float y0, float x1, float y1, Color c) {
    const auto base = static_cast<quint32>(v_.size());
    v_.resize(v_.size() + 4);
    QSGGeometry::ColoredPoint2D *p = v_.data() + base;
    p[0].set(x0, y0, c.r, c.g, c.b, c.a);
    p[1].set(x1, y0, c.r, c.g, c.b, c.a);
    p[2].set(x0, y1, c.r, c.g, c.b, c.a);
    p[3].set(x1, y1, c.r, c.g, c.b, c.a);
    const quint32 idx[6] = {base,     base + 1, base + 2,
                            base + 1, base + 2, base + 3};
    i_.insert(i_.end(), idx, idx + 6);
  }
  void vline(float x, float y0, float y1, Color c) {
    quad(x, y0, x + 1, y1, c);
  }
  void hline(float x0, float x1, float y, Color c) {
    quad(x0, y, x1, y + 1, c);
  }
  /// 1 px border: an outer quad in `line` under an inset quad in `fill`. Relies
  /// on painter's order inside one node, so the material must keep blending
  /// enabled.
  void outlined(float x0, float y0, float x1, float y1, Color fill,
                Color line) {
    quad(x0, y0, x1, y1, line);
    if (x1 - x0 > 2 && y1 - y0 > 2)
      quad(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
  }
  /// Translucent premultiplied quad.
  void quadAlpha(float x0, float y0, float x1, float y1, Color c) {
    const auto m = [&](std::uint8_t x) {
      return static_cast<std::uint8_t>(x * c.a / 255);
    };
    quad(x0, y0, x1, y1, {m(c.r), m(c.g), m(c.b), c.a});
  }

  /// Start of a strip of vertical min/max columns (two vertices per column).
  quint32 stripBegin() const { return static_cast<quint32>(v_.size()); }
  void stripColumn(float x, float ya, float yb, Color c) {
    v_.resize(v_.size() + 2);
    QSGGeometry::ColoredPoint2D *p = v_.data() + v_.size() - 2;
    p[0].set(x, ya, c.r, c.g, c.b, c.a);
    p[1].set(x, yb, c.r, c.g, c.b, c.a);
  }
  void stripEnd(quint32 base) {
    const auto n = (static_cast<quint32>(v_.size()) - base) / 2;
    for (quint32 k = 0; k + 1 < n; ++k) {
      const quint32 a = base + 2 * k;
      const quint32 idx[6] = {a, a + 1, a + 2, a + 1, a + 2, a + 3};
      i_.insert(i_.end(), idx, idx + 6);
    }
  }

  /// Copies the mesh into the node's geometry. Returns the number of vertices
  /// written. `frontToBack` reverses the quad order, for nodes made with
  /// `makeGeometryNode(true)`: in the opaque pass the FIRST primitive drawn at
  /// a pixel wins (depth test, one z per node), the opposite of painter's
  /// order.
  int commit(QSGGeometryNode *node, bool frontToBack = false) {
    if (v_.empty()) {
      // A zero-vertex node is avoided on purpose: one invisible degenerate
      // triangle.
      v_.assign(3, QSGGeometry::ColoredPoint2D{0, 0, 0, 0, 0, 0});
      i_.assign({0u, 1u, 2u});
    }
    if (frontToBack)
      for (std::size_t a = 0, b = i_.size(); a + 6 < b; a += 6, b -= 6)
        std::swap_ranges(i_.begin() + static_cast<std::ptrdiff_t>(a),
                         i_.begin() + static_cast<std::ptrdiff_t>(a + 6),
                         i_.begin() + static_cast<std::ptrdiff_t>(b - 6));
    QSGGeometry *g = node->geometry();
    g->allocate(static_cast<int>(v_.size()), static_cast<int>(i_.size()));
    std::memcpy(g->vertexData(), v_.data(),
                v_.size() * sizeof(QSGGeometry::ColoredPoint2D));
    std::memcpy(g->indexData(), i_.data(), i_.size() * sizeof(quint32));
    node->markDirty(QSGNode::DirtyGeometry);
    return static_cast<int>(v_.size());
  }

private:
  std::vector<QSGGeometry::ColoredPoint2D> v_;
  std::vector<quint32> i_;
};

/// `opaque` nodes go to the scene graph's opaque pass (front-to-back, depth
/// tested, no blending). Used for the backgrounds only; see `Mesh::commit` for
/// the ordering consequence.
inline QSGGeometryNode *makeGeometryNode(bool opaque = false) {
  auto *node = new QSGGeometryNode;
  auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0,
                            0, QSGGeometry::UnsignedIntType);
  g->setDrawingMode(QSGGeometry::DrawTriangles);
  g->setVertexDataPattern(QSGGeometry::DynamicPattern);
  g->setIndexDataPattern(QSGGeometry::DynamicPattern);
  node->setGeometry(g);
  node->setFlag(QSGNode::OwnsGeometry);
  auto *material = new QSGVertexColorMaterial;
  material->setFlag(QSGMaterial::Blending, !opaque);
  node->setMaterial(material);
  node->setFlag(QSGNode::OwnsMaterial);
  Mesh empty;
  empty.commit(node);
  return node;
}

/// Experiment knob: LINA_BG_ALPHA=1 puts the background nodes into the alpha
/// pass like everything else.
inline bool opaqueBackgrounds() {
  static const bool v = !qEnvironmentVariableIsSet("LINA_BG_ALPHA");
  return v;
}

inline QSGClipNode *makeClipNode() {
  auto *clip = new QSGClipNode;
  auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
  g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
  clip->setGeometry(g);
  clip->setFlag(QSGNode::OwnsGeometry);
  clip->setIsRectangular(true);
  return clip;
}

inline void setClipRect(QSGClipNode *clip, const QRectF &r) {
  if (clip->clipRect() == r)
    return;
  QSGGeometry::updateRectGeometry(clip->geometry(), r);
  clip->setClipRect(r);
  clip->markDirty(QSGNode::DirtyGeometry);
}

/// Shaped single-line layouts, kept so that a label is shaped once and re-added
/// to a QSGTextNode on every rebuild without going through HarfBuzz again.
class LayoutCache {
public:
  void setFont(const QFont &font) {
    font_ = font;
    map_.clear();
  }
  template <typename MakeText>
  QTextLayout *get(std::uint64_t key, MakeText &&makeText) {
    auto &slot = map_[key];
    if (!slot) {
      slot = std::make_unique<QTextLayout>(makeText(), font_);
      slot->setCacheEnabled(true);
      slot->beginLayout();
      QTextLine line = slot->createLine();
      line.setLineWidth(100000);
      line.setPosition({0, 0});
      slot->endLayout();
    }
    return slot.get();
  }
  std::size_t size() const { return map_.size(); }
  void clear() { map_.clear(); }

private:
  QFont font_;
  std::unordered_map<std::uint64_t, std::unique_ptr<QTextLayout>> map_;
};

} // namespace sg
