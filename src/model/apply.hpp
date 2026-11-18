// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_model: the two functions that change a project. Only
// `Document` calls them.
#ifndef LINA_MODEL_SRC_APPLY_HPP
#define LINA_MODEL_SRC_APPLY_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/command.hpp"
#include "lina/model/detail/inverse.hpp"
#include "lina/model/fault.hpp"
#include "lina/model/project.hpp"

#include <expected>

namespace lina::model::detail {

// What applying a command produced: what takes it back, and what changed. When
// `changes` is empty the project is as it was and `inverse` means nothing.
struct Applied {
  Inverse inverse;
  ChangeSet changes;
};

// Applies one command. On failure the project is untouched. A command that
// asks for what is already the case succeeds with an empty change set.
std::expected<Applied, Failure> applyCommand(Project &project,
                                             const Command &command);

// Applies an inverse this document recorded, with the project in the state
// that inverse was recorded for. That always applies. If it does not, a
// command's inverse is wrong, which is a defect in this library and not
// something a caller can cause or handle: the process is stopped (a message on
// standard error, then std::abort). Carrying on instead would leave a project
// that is half-way through an undo, matches no state the user ever saw and no
// longer matches its own history, and the next save would make that
// permanent. Stopping keeps the last saved project and the recovery copy as
// they are.
Applied applyInverse(Project &project, const Inverse &inverse);

} // namespace lina::model::detail

#endif // LINA_MODEL_SRC_APPLY_HPP
