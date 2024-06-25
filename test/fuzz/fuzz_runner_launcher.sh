#!/usr/bin/env bash
#
# Launcher for the fuzz runner engine.
# See https://github.com/bazelbuild/rules_fuzzing for more info.

if (( ! FUZZER_IS_REGRESSION )); then
    echo "NOTE: Non-regression mode is not supported by this engine."
fi

command_line=("${FUZZER_BINARY}")
if [[ -n "${FUZZER_SEED_CORPUS_DIR}" ]]; then
    command_line+=("${FUZZER_SEED_CORPUS_DIR}")
fi

exec "${command_line[@]}"
