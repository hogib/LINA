#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks that every C++ file of LINA's own matches the checked-in .clang-format.
# Third-party code (a submodule) and build output (git-ignored) are never listed.
#
# Usage: tools/check-format.sh          check only; exits 1 if a file is not formatted
#        tools/check-format.sh --fix    reformat in place
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
clang_format="${CLANG_FORMAT:-clang-format}"

mode=(--dry-run --Werror)
case "${1:-}" in
"") ;;
--fix) mode=(-i) ;;
*)
  echo "usage: $0 [--fix]" >&2
  exit 2
  ;;
esac

# The listing below runs in a process substitution, whose failure the shell does not see.
# Fail here instead if git cannot read the repository.
git -C "$repo" rev-parse --git-dir >/dev/null

files=()
while IFS= read -r -d '' file; do
  # A tracked file that was deleted in the working tree is still listed; skip it.
  [[ -f "$repo/$file" ]] && files+=("$repo/$file")
done < <(git -C "$repo" ls-files -z --cached --others --exclude-standard -- \
  '*.cpp' '*.hpp' '*.h')

if ((${#files[@]} == 0)); then
  echo "check-format: FAIL: found no C++ files to check" >&2
  exit 1
fi

"$clang_format" --style=file "${mode[@]}" "${files[@]}"
echo "check-format: ${#files[@]} file(s) checked"
