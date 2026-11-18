// SPDX-License-Identifier: GPL-3.0-or-later
#include "apply.hpp"

#include "apply_parts.hpp"

#include <cstdlib>
#include <iostream>
#include <variant>

namespace lina::model::detail {

namespace {

[[noreturn]] void stopOnRefusedInverse(const Failure &failure) {
  std::cerr << "lina_model: an undo step was refused ("
            << faultName(failure.fault) << " on object " << failure.object.id
            << "); the document cannot be trusted any more, stopping.\n";
  std::abort();
}

Applied replay(Project &project, const Command &command) {
  auto applied = applyCommand(project, command);
  if (!applied.has_value()) {
    stopOnRefusedInverse(applied.error());
  }
  return std::move(*applied);
}

template <class Restore>
Applied replay(Project &project, const Restore &restore) {
  return restoreOne(project, restore);
}

} // namespace

std::expected<Applied, Failure> applyCommand(Project &project,
                                             const Command &command) {
  return std::visit(
      [&project](const auto &typed) { return applyOne(project, typed); },
      command);
}

Applied applyInverse(Project &project, const Inverse &inverse) {
  return std::visit(
      [&project](const auto &typed) { return replay(project, typed); },
      inverse);
}

} // namespace lina::model::detail
