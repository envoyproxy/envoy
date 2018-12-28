

# set -e forces the script to exit on non-zero exit codes. Set +e makes it easier to
# catch the non-zero exit code.
set +e
disableHeapCheck

start_test Launching envoy with no parameters. Check the exit value is 1
(set -x; ${ENVOY_BIN} --base-id "${BASE_ID}")
EXIT_CODE=$?
# The test should fail if the Envoy binary exits with anything other than 1.
if [[ $EXIT_CODE -ne 1 ]]; then
    echo "Envoy exited with code: ${EXIT_CODE}"
    exit 1
fi

enableHeapCheck
set -e

