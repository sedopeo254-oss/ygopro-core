#!/usr/bin/env sh
set -eu
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_file="${TMPDIR:-/tmp}/ygopro-duel-succession-transfer-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root_dir" "$root_dir/tests/duel_succession_transfer_tests.cpp" \
  -o "$output_file"
"$output_file"
