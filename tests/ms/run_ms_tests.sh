#!/bin/bash
# Headless MiniScript test driver.
#
# Runs every test_*.ms script under tests/ms/ against the built
# raylib-miniscript binary and reports pass/fail per script, aggregating
# into a single process exit code -- the C++ tests (fs_tests, interp_tests)
# already answer "which side a failure is on" for engine-level bugs; this is
# the other half, for bugs in the project's own .ms scripts.

set -u
cd "$(dirname "$0")/../.."   # repo root of raylib-miniscript

BIN=build/raylib-miniscript
if [ ! -x "$BIN" ]; then
    echo "raylib-miniscript binary not found at $BIN -- build it first." >&2
    exit 1
fi

FAILED=0
COUNT=0

for script in tests/ms/test_*.ms; do
    [ -e "$script" ] || continue
    COUNT=$((COUNT + 1))
    echo "=== $script ==="
    "$BIN" "$script"
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "=== $script: FAILED (exit $rc) ==="
        FAILED=$((FAILED + 1))
    else
        echo "=== $script: passed ==="
    fi
    echo
done

echo "$COUNT MiniScript test scripts run, $FAILED failed"
[ "$FAILED" -eq 0 ]
