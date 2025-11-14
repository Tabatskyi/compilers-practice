#!/usr/bin/env bash
set -uo pipefail

# Simple build and test script using clang-20
# Usage: bash scripts/build_and_test.sh

# Ensure we're at repo root
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

# Compiler
if command -v clang++-20 >/dev/null 2>&1; then
  CXX=clang++-20
else
  echo "ERROR: clang++-20 not found in PATH. Please install clang-20 or adjust the script." >&2
  echo "Hint (Ubuntu): sudo apt-get update && sudo apt-get install -y clang-20" >&2
  exit 1
fi

CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -Wpedantic)
SRC=(SyntaxParser.cpp compiler.cpp)
BIN_DIR="bin"
mkdir -p "$BIN_DIR"
OUT="$BIN_DIR/compiler"

echo "[BUILD] $CXX ${CXXFLAGS[*]} -> $OUT"
"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT"

echo "[TEST] Running sample programs and comparing LLVM IR"
TMP_DIR=".test-out"
mkdir -p "$TMP_DIR"

pass=0
fail=0

declare -a CASES=(
  "prog_ok.txt:out_ok.ll"
  "prog_struct_fn.txt:out_struct_fn.ll"
  "prog_member.txt:out_member.ll"
)

for case in "${CASES[@]}"; do
  IFS=":" read -r src expected <<<"$case"
  name="$(basename "$src" .txt)"
  actual="$TMP_DIR/${name}.ll"
  echo "- $src -> $actual"
  "$OUT" "$src" "$actual" 1>/dev/null || { echo "  FAIL: compiler exited non-zero for $src" >&2; ((fail++)); continue; }
  if diff -u "$expected" "$actual" >/dev/null; then
    echo "  OK: $src matches $expected"
    ((pass++))
  else
    echo "  FAIL: $src differs from $expected" >&2
    diff -u "$expected" "$actual" || true
    ((fail++))
  fi
done

echo
echo "Summary: $pass passed, $fail failed"
if (( fail > 0 )); then
  exit 2
fi

echo "All tests passed."
