#!/bin/bash
set -e
cd "$(dirname "$0")"
cmake -DBUILD_TESTS=ON -S . -B build
cmake --build build
