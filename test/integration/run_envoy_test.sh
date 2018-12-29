#!/bin/bash

source "$TEST_RUNDIR/test/integration/test_utility.sh"

start_test Launching envoy with a bogus command line flag.
${ENVOY_BIN} --bogus-flag
EXIT_CODE=$?
# The test should fail if the Envoy binary exits with anything other than 1.
if [[ $EXIT_CODE -ne 1 ]]; then
    echo "Envoy exited with code: ${EXIT_CODE}"
    exit 1
fi

start_test Launching envoy with no args starts a server properly.
log="${TEST_TMPDIR}/envoy.log"
rm -f "$log"
${ENVOY_BIN} >& "$log" &
envoy_pid="$!"
until grep "starting main dispatch loop" $log; do
  sleep .2
done
kill -HUP "$envoy_pid"

start_test Launching envoy with a valid base ID starts a server properly.
rm -f "$log"
${ENVOY_BIN} --base-id 1234 >& "$log" &
envoy_pid="$!"
until grep "starting main dispatch loop" $log; do
  sleep .2
done
kill -HUP "$envoy_pid"
