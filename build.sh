#!/usr/bin/env bash
set -eu
cd -- "$(dirname -- "$0")"
: "${IDF_PATH:?Activate ESP-IDF 6.0.2 before building}"
idf.py build
