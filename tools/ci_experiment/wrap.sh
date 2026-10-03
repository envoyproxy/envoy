#!/bin/bash
# CI experiment for #47485 only: run a test and print its memory maps so a
# crash backtrace can be rebased and symbolized offline.
maps="$(mktemp)"
"$@" &
pid=$!
while kill -0 "$pid" 2>/dev/null; do
    cp "/proc/${pid}/maps" "$maps" 2>/dev/null
    sleep 0.01
done
wait "$pid"
status=$?
echo "=== CI_EXPERIMENT_MAPS ==="
cat "$maps"
echo "=== CI_EXPERIMENT_MAPS_END ==="
exit "$status"
