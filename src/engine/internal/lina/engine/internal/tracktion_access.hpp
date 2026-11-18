// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP
#define LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP

#include "lina/engine/engine.hpp"

#include <tracktion_engine/tracktion_engine.h>

namespace lina::engine::internal {

/// The Tracktion objects behind an Engine.
///
/// For code inside the engine tier only: the projection, and tests that build
/// an edit by hand. This header is not on lina_engine's public include path;
/// a target that needs it links lina::engine_internals.
struct TracktionAccess {
  static tracktion::Engine &engine(Engine &engine);
  static tracktion::Edit &edit(Engine &engine);
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_TRACKTION_ACCESS_HPP
