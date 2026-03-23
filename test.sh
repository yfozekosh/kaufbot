#!/bin/bash
set -e
cd "$(dirname "$0")/build"
ctest --output-on-failure
