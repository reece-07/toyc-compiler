#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
COMPILER="$ROOT/build/toyc"
RUNNER="$ROOT/tests/riscv_runner.py"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if [ ! -x "$COMPILER" ]; then
  echo "compiler binary not found: $COMPILER" >&2
  exit 1
fi

pass_count=0
fail_count=0

run_pass_test() {
  source_file=$1
  output_file=$(mktemp)
  expected_exit_file=${source_file%.tc}.exit

  if "$COMPILER" --input "$source_file" --output "$output_file" >/dev/null 2>&1 &&
     grep -q '\.globl main' "$output_file" &&
     ! grep -q 'Placeholder' "$output_file"; then
    if [ -f "$expected_exit_file" ]; then
      actual_exit=$("$PYTHON_BIN" "$RUNNER" "$output_file")
      expected_exit=$(cat "$expected_exit_file")
      if [ "$actual_exit" = "$expected_exit" ]; then
        echo "PASS  $(basename "$source_file")"
        pass_count=$((pass_count + 1))
      else
        echo "FAIL  $(basename "$source_file")"
        echo "  expected exit: $expected_exit"
        echo "  actual exit:   $actual_exit"
        fail_count=$((fail_count + 1))
      fi
    else
      echo "PASS  $(basename "$source_file")"
      pass_count=$((pass_count + 1))
    fi
  else
    echo "FAIL  $(basename "$source_file")"
    fail_count=$((fail_count + 1))
  fi

  rm -f "$output_file"
}

run_fail_test() {
  source_file=$1
  expected_file=${source_file%.tc}.expected
  error_file=$(mktemp)

  if "$COMPILER" --input "$source_file" >/dev/null 2>"$error_file"; then
    echo "FAIL  $(basename "$source_file")"
    fail_count=$((fail_count + 1))
  elif grep -F -q "$(cat "$expected_file")" "$error_file"; then
    echo "PASS  $(basename "$source_file")"
    pass_count=$((pass_count + 1))
  else
    echo "FAIL  $(basename "$source_file")"
    echo "  expected substring: $(cat "$expected_file")"
    echo "  actual output:"
    sed 's/^/  /' "$error_file"
    fail_count=$((fail_count + 1))
  fi

  rm -f "$error_file"
}

for source_file in "$ROOT"/tests/pass/*.tc; do
  run_pass_test "$source_file"
done

for source_file in "$ROOT"/tests/fail/*.tc; do
  run_fail_test "$source_file"
done

echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -ne 0 ]; then
  exit 1
fi
