#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Clean build ==="
rm -rf build

echo "=== Configure (with tests, coverage) ==="
cmake -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -S . -B build

echo "=== Build ==="
cmake --build build

echo "=== Test ==="
cd build && ctest --output-on-failure && cd ..

echo "=== Format check ==="
cmake --build build --target format-check

echo "=== Lint (clang-tidy) ==="
cmake --build build --target lint

echo "=== Lint (cppcheck) ==="
cmake --build build --target cppcheck

echo "=== Coverage report ==="
cmake --build build --target coverage

COVERAGE=$(lcov --summary build/coverage_filtered.info 2>&1 | grep 'lines' | sed 's/.*: \([0-9.]*\)%.*/\1/')
echo "=== Line coverage: ${COVERAGE}% ==="
if awk "BEGIN {exit !($COVERAGE < 60)}"; then
    echo "ERROR: Coverage ${COVERAGE}% is below 60% threshold"
    exit 1
fi

echo "=== CI complete ==="
