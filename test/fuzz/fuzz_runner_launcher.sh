#!/bin/bash
#
# Launcher for the fuzz runner engine.

if (( ! FUZZER_IS_REGRESSION )); then
    echo "NOTE: Non-regression mode is not supported by this engine."
fi

command_line=("${FUZZER_BINARY}")
if [[ -n "${FUZZER_SEED_CORPUS_DIR}" ]]; then
    command_line+=("${FUZZER_SEED_CORPUS_DIR}")
fi

exec "${command_line[@]}"
