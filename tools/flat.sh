#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# flat: "FLAT Lints All Translation-units".
#
# Runs clang-tidy with the checked-in .clang-tidy over LINA's own translation units.
# Third-party code (a submodule) and build output (git-ignored) are never listed.
#
# Usage: tools/flat.sh [build-dir]
#
# build-dir must hold a compile_commands.json written by a Clang preset (default: build/asan),
# so that the flags clang-tidy replays are flags Clang understands. Configure it first:
#   cmake --preset asan
# Generated sources (none yet) would additionally need a build.
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo/build/asan}"
clang_tidy="${CLANG_TIDY:-clang-tidy}"
# Half the cores by default, so a local run leaves the machine usable.
jobs="${LINA_JOBS:-$((($(nproc) + 1) / 2))}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
  echo "flat: $build_dir/compile_commands.json not found; configure a Clang preset first" >&2
  exit 2
fi

# The listing below runs in a process substitution, whose failure the shell does not see.
# Fail here instead if git cannot read the repository.
git -C "$repo" rev-parse --git-dir >/dev/null

files=()
while IFS= read -r -d '' file; do
  [[ -f "$repo/$file" ]] && files+=("$repo/$file")
done < <(git -C "$repo" ls-files -z --cached --others --exclude-standard -- '*.cpp')

if ((${#files[@]} == 0)); then
  echo "flat: FAIL: found no C++ sources to check" >&2
  exit 1
fi

printf '%s\0' "${files[@]}" |
  xargs -0 -n 1 -P "$jobs" "$clang_tidy" -p "$build_dir" --quiet
echo "flat: ${#files[@]} file(s) checked"
