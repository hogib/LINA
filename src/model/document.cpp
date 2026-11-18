// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/document.hpp"

#include "apply.hpp"

#include <algorithm>
#include <utility>

namespace lina::model {

namespace {

std::unexpected<Failure> fail(Fault fault) {
  return std::unexpected(Failure{.fault = fault, .object = projectRef});
}

} // namespace

Document::Document() : project_(makeEmptyProject()) {}

Document::Document(Project project) : project_(std::move(project)) {}

std::expected<Document, std::vector<Failure>>
Document::fromProject(Project project) {
  std::vector<Failure> failures = validate(project);
  if (!failures.empty()) {
    return std::unexpected(std::move(failures));
  }
  return Document{std::move(project)};
}

std::expected<void, Failure> Document::begin(std::string name) {
  if (open_.has_value()) {
    return fail(Fault::TransactionOpen);
  }
  open_ = OpenTransaction{
      .name = std::move(name),
      .inverses = {},
      .changes = {},
  };
  return {};
}

std::expected<ChangeSet, Failure> Document::apply(const Command &command) {
  if (!open_.has_value()) {
    return fail(Fault::NoTransaction);
  }
  auto applied = detail::applyCommand(project_, command);
  if (!applied.has_value()) {
    return std::unexpected(applied.error());
  }
  if (!applied->changes.empty()) {
    open_->inverses.push_back(std::move(applied->inverse));
    open_->changes.merge(applied->changes);
  }
  return std::move(applied->changes);
}

std::expected<void, Failure> Document::commit() {
  if (!open_.has_value()) {
    return fail(Fault::NoTransaction);
  }
  if (!open_->changes.empty()) {
    std::ranges::reverse(open_->inverses);
    undo_.push_back({
        .name = std::move(open_->name),
        .inverses = std::move(open_->inverses),
    });
    redo_.clear();
  }
  open_.reset();
  return {};
}

std::expected<ChangeSet, Failure> Document::rollback() {
  if (!open_.has_value()) {
    return fail(Fault::NoTransaction);
  }
  std::ranges::reverse(open_->inverses);
  ChangeSet changes;
  replay(open_->inverses, changes);
  open_.reset();
  return changes;
}

std::expected<ChangeSet, Failure>
Document::perform(std::string name, std::span<const Command> commands) {
  if (auto begun = begin(std::move(name)); !begun.has_value()) {
    return std::unexpected(begun.error());
  }
  ChangeSet changes;
  for (const Command &command : commands) {
    auto applied = apply(command);
    if (!applied.has_value()) {
      // What the rollback changed is of no use to the caller: the document is
      // back where it was before this call.
      return rollback().and_then(
          [&applied](const ChangeSet &) -> std::expected<ChangeSet, Failure> {
            return std::unexpected(applied.error());
          });
    }
    changes.merge(*applied);
  }
  return commit().transform([&changes] { return std::move(changes); });
}

std::expected<ChangeSet, Failure> Document::perform(std::string name,
                                                    const Command &command) {
  return perform(std::move(name), std::span<const Command>{&command, 1});
}

std::optional<std::string_view> Document::undoName() const {
  return undo_.empty() ? std::nullopt
                       : std::optional<std::string_view>{undo_.back().name};
}

std::optional<std::string_view> Document::redoName() const {
  return redo_.empty() ? std::nullopt
                       : std::optional<std::string_view>{redo_.back().name};
}

std::expected<ChangeSet, Failure> Document::undo() {
  return step(undo_, redo_, Fault::NothingToUndo);
}

std::expected<ChangeSet, Failure> Document::redo() {
  return step(redo_, undo_, Fault::NothingToRedo);
}

std::vector<detail::Inverse>
Document::replay(const std::vector<detail::Inverse> &inverses,
                 ChangeSet &changes) {
  std::vector<detail::Inverse> back;
  back.reserve(inverses.size());
  for (const detail::Inverse &inverse : inverses) {
    detail::Applied applied = detail::applyInverse(project_, inverse);
    back.push_back(std::move(applied.inverse));
    changes.merge(applied.changes);
  }
  std::ranges::reverse(back);
  return back;
}

std::expected<ChangeSet, Failure> Document::step(std::vector<Step> &from,
                                                 std::vector<Step> &to,
                                                 Fault whenEmpty) {
  if (open_.has_value()) {
    return fail(Fault::TransactionOpen);
  }
  if (from.empty()) {
    return fail(whenEmpty);
  }
  Step taken = std::move(from.back());
  from.pop_back();
  ChangeSet changes;
  to.push_back({
      .name = std::move(taken.name),
      .inverses = replay(taken.inverses, changes),
  });
  return changes;
}

} // namespace lina::model
