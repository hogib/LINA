// SPDX-License-Identifier: GPL-3.0-or-later
//
// The document: a project together with its undo history. This is the one
// place a project in use can change (docs/design/13-architecture.md). Readers
// get `const Project &`; every edit is a command applied inside a transaction,
// and one transaction is one undo step.
//
// Undo works by inverses: applying a command yields what takes it back, and
// undoing a transaction applies those, newest first. The history is not
// bounded.
#ifndef LINA_MODEL_DOCUMENT_HPP
#define LINA_MODEL_DOCUMENT_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/command.hpp"
#include "lina/model/detail/inverse.hpp"
#include "lina/model/fault.hpp"
#include "lina/model/project.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lina::model {

class Document {
public:
  // An empty project: a master track and nothing else.
  Document();

  // Takes over a project built elsewhere (by the loader, by a test). Fails with
  // everything `validate` finds when the project breaks a rule.
  static std::expected<Document, std::vector<Failure>>
  fromProject(Project project);

  [[nodiscard]] const Project &project() const { return project_; }

  // --- Editing ---

  // Opens a transaction: one user gesture, shown in undo and redo under
  // `name`.
  std::expected<void, Failure> begin(std::string name);

  // Applies one command inside the open transaction and says what changed. On
  // failure the document is exactly as it was, and the transaction stays open.
  // A command that changes nothing costs nothing: it is not remembered, so a
  // long drag that repeats positions does not grow the history.
  std::expected<ChangeSet, Failure> apply(const Command &command);

  // Closes the open transaction and makes it the newest undo step, which
  // empties the redo history. A transaction whose commands, taken together,
  // report no change leaves no step and keeps the redo history: nothing was
  // applied, or everything that was created was removed again. (Identities
  // handed out on the way stay handed out.) A transaction that sets a property
  // and then sets it back still reports that property and does leave a step:
  // telling that apart would mean comparing documents.
  std::expected<void, Failure> commit();

  // Closes the open transaction by taking back everything it applied.
  std::expected<ChangeSet, Failure> rollback();

  // begin, apply each, commit: or, when a command fails, rollback and that
  // failure. The returned change set covers the whole transaction.
  std::expected<ChangeSet, Failure> perform(std::string name,
                                            std::span<const Command> commands);
  std::expected<ChangeSet, Failure> perform(std::string name,
                                            const Command &command);

  [[nodiscard]] bool inTransaction() const { return open_.has_value(); }

  // --- History ---

  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  // The name of the step that undo or redo would act on.
  [[nodiscard]] std::optional<std::string_view> undoName() const;
  [[nodiscard]] std::optional<std::string_view> redoName() const;

  // Takes back the newest step, or applies again the step undone last. Not
  // available while a transaction is open.
  std::expected<ChangeSet, Failure> undo();
  std::expected<ChangeSet, Failure> redo();

private:
  // One undo or redo step: what takes it back (or applies it again), in the
  // order it must be applied.
  struct Step {
    std::string name;
    std::vector<detail::Inverse> inverses;
  };

  struct OpenTransaction {
    std::string name;
    // What takes back the commands applied so far, oldest first.
    std::vector<detail::Inverse> inverses;
    // Everything those commands changed, merged.
    ChangeSet changes;
  };

  explicit Document(Project project);

  // Applies inverses in order and returns what takes them back, in the order
  // that does so. Inverses the document recorded always apply; see
  // `applyInverse` in the sources for what happens if one does not.
  std::vector<detail::Inverse>
  replay(const std::vector<detail::Inverse> &inverses, ChangeSet &changes);

  // Applies the newest step of `from` and records the step that reverses it in
  // `to`.
  std::expected<ChangeSet, Failure>
  step(std::vector<Step> &from, std::vector<Step> &to, Fault whenEmpty);

  Project project_;
  std::optional<OpenTransaction> open_;
  std::vector<Step> undo_;
  std::vector<Step> redo_;
};

} // namespace lina::model

#endif // LINA_MODEL_DOCUMENT_HPP
