#!/bin/bash

source test_utility.sh

start_test Launching envoy with a bogus command line flag.
${ENVOY_BIN} --bogus-flag
EXIT_CODE=$?
# The test should fail if the Envoy binary exits with anything other than 1.
if [[ $EXIT_CODE -ne 1 ]]; then
    echo "Envoy exited with code: ${EXIT_CODE}"
    exit 1
fi
