#!/usr/bin/env bash
# Differential test (lifted C vs Unicorn) on sampled leaf functions. See tools/difftest.py.
cd "$(dirname "$0")/.." || exit 1
exec .venv/bin/python tools/difftest.py "$@"
