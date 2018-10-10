#!/bin/bash

set -e

# The following functions are borrowed from PageSpeed's system test
# infrastructure, where are part of a bash system-test helper library.
# I'm putting them here for this one test to help me debug it, with
# the thinking that if there's traction for this infrastructure I'll
# factor it out into a helpers class for Envoy.  Original source link:
#
# https://github.com/apache/incubator-pagespeed-mod/blob/c7cc4f22c79ada8077be2a16afc376dc8f8bd2da/pagespeed/automatic/system_test_helpers.sh#L383

CURRENT_TEST="NONE"
function start_test() {
  CURRENT_TEST="$@"
  echo "TEST: $CURRENT_TEST"
}

check() {
  echo "     check" "$@" ...
  "$@" || handle_failure
}

BACKGROUND_PID="?"
run_in_background_saving_pid() {
  echo "     backgrounding:" "$@" ...
  "$@" &
  BACKGROUND_PID="$!"
}

# By default, print a message like:
#   failure at line 374
#   FAIL
# and then exit with return value 1.  If we expected this test to fail, log to
# $EXPECTED_FAILURES and return without exiting.
#
# If the shell does not support the 'caller' builtin, skip the line number info.
#
# Assumes it's being called from a failure-reporting function and that the
# actual failure the user is interested in is our caller's caller.  If it
# weren't for this, fail and handle_failure could be the same.
handle_failure() {
  if [ $# -eq 1 ]; then
    echo FAILed Input: "$1"
  fi

  # From http://stackoverflow.com/questions/685435/bash-stacktrace
  # to avoid printing 'handle_failure' we start with 1 to skip get_stack caller
  local i
  local stack_size=${#FUNCNAME[@]}
  for (( i=1; i<$stack_size ; i++ )); do
    local func="${FUNCNAME[$i]}"
    [ -z "$func" ] && func=MAIN
    local line_number="${BASH_LINENO[(( i - 1 ))]}"
    local src="${BASH_SOURCE[$i]}"
    [ -z "$src" ] && src=non_file_source
    echo "${src}:${line_number}: $func"
  done

  # Note: we print line number after "failed input" so that it doesn't get
  # knocked out of the terminal buffer.
  if type caller > /dev/null 2>&1 ; then
    # "caller 1" is our caller's caller.
    echo "     failure at line $(caller 1 | sed 's/ .*//')" 1>&2
  fi
  echo "in '$CURRENT_TEST'"
  echo FAIL.
  exit 1
}

# The heapchecker outputs some data to stderr on every execution.  This gets intermingled
# with the output from --hot-restart-version, so disable the heap-checker for these runs.
disableHeapCheck () {
  SAVED_HEAPCHECK=${HEAPCHECK}
  unset HEAPCHECK
}

enableHeapCheck () {
  HEAPCHECK=${SAVED_HEAPCHECK}
}


[[ -z "${ENVOY_BIN}" ]] && ENVOY_BIN="${TEST_RUNDIR}"/source/exe/envoy-static

# TODO(htuch): In this test script, we are duplicating work done in test_environment.cc via sed.
# Instead, we can add a simple C++ binary that links against test_environment.cc and uses the
# substitution methods provided there.
JSON_TEST_ARRAY=()

# Parameterize IPv4 and IPv6 testing.
if [[ -z "${ENVOY_IP_TEST_VERSIONS}" ]] || [[ "${ENVOY_IP_TEST_VERSIONS}" == "all" ]] \
  || [[ "${ENVOY_IP_TEST_VERSIONS}" == "v4only" ]]; then
  HOT_RESTART_JSON_V4="${TEST_TMPDIR}"/hot_restart_v4.yaml
  echo building ${HOT_RESTART_JSON_V4} ...
  cat "${TEST_RUNDIR}"/test/config/integration/server.yaml |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_RUNDIR#" | \
    sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
    sed -e "s#{{ dns_lookup_family }}#V4_ONLY#" | \
    cat > "${HOT_RESTART_JSON_V4}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V4}")
fi

if [[ -z "${ENVOY_IP_TEST_VERSIONS}" ]] || [[ "${ENVOY_IP_TEST_VERSIONS}" == "all" ]] \
  || [[ "${ENVOY_IP_TEST_VERSIONS}" == "v6only" ]]; then
  HOT_RESTART_JSON_V6="${TEST_TMPDIR}"/hot_restart_v6.yaml
  cat "${TEST_RUNDIR}"/test/config/integration/server.yaml |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_RUNDIR#" | \
    sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#::1#" | \
    sed -e "s#{{ dns_lookup_family }}#v6_only#" | \
    cat > "${HOT_RESTART_JSON_V6}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V6}")
fi

# Also test for listening on UNIX domain sockets. We use IPv4 for the
# upstreams to avoid too much wild sedding.
HOT_RESTART_JSON_UDS="${TEST_TMPDIR}"/hot_restart_uds.yaml
SOCKET_DIR="$(mktemp -d /tmp/envoy_test_hotrestart.XXXXXX)"
cat "${TEST_RUNDIR}"/test/config/integration/server_unix_listener.yaml |
  sed -e "s#{{ socket_dir }}#${SOCKET_DIR}#" | \
  sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
  cat > "${HOT_RESTART_JSON_UDS}"
JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_UDS}")

# Enable this test to work with --runs_per_test
if [[ -z "${TEST_RANDOM_SEED}" ]]; then
  BASE_ID=1
else
  BASE_ID="${TEST_RANDOM_SEED}"
fi

echo "Hot restart test using --base-id ${BASE_ID}"

TEST_INDEX=0
for HOT_RESTART_JSON in "${JSON_TEST_ARRAY[@]}"
do
  # TODO(jun03): instead of setting the base-id, the validate server should use the nop hot restart
  start_test validation
  check "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" --mode validate --service-cluster cluster \
      --max-obj-name-len 500 --service-node node --base-id "${BASE_ID}"

  # Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
  # sanity test.
  start_test Starting epoch 0
  ADMIN_ADDRESS_PATH_0="${TEST_TMPDIR}"/admin.0."${TEST_INDEX}".address
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
      --restart-epoch 0 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --max-obj-name-len 500 --admin-address-path "${ADMIN_ADDRESS_PATH_0}"

  FIRST_SERVER_PID=$BACKGROUND_PID

  start_test Updating original config listener addresses
  sleep 3
  UPDATED_HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart_updated."${TEST_INDEX}".yaml
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_0}" \
    "-u" "${UPDATED_HOT_RESTART_JSON}"

  # Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
  # get eaten.
  echo "Sending SIGUSR1/SIGHUP to first server"
  kill -SIGUSR1 ${FIRST_SERVER_PID}
  kill -SIGHUP ${FIRST_SERVER_PID}
  sleep 3

  disableHeapCheck

  # To ensure that we don't accidentally change the /hot_restart_version
  # string, compare it against a hard-coded string.
  start_test Checking for consistency of /hot_restart_version
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" 2>&1)
  EXPECTED_CLI_HOT_RESTART_VERSION="10.200.16384.127.options=capacity=16384, num_slots=8209 hash=228984379728933363 size=2654312"
  check [ "${CLI_HOT_RESTART_VERSION}" = "${EXPECTED_CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for consistency of /hot_restart_version with --max-obj-name-len 500
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --max-obj-name-len 500 2>&1)
  EXPECTED_CLI_HOT_RESTART_VERSION="10.200.16384.567.options=capacity=16384, num_slots=8209 hash=228984379728933363 size=9863272"
  check [ "${CLI_HOT_RESTART_VERSION}" = "${EXPECTED_CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for match of --hot-restart-version and admin /hot_restart_version
  ADMIN_ADDRESS_0=$(cat "${ADMIN_ADDRESS_PATH_0}")
  echo fetching hot restart version from http://${ADMIN_ADDRESS_0}/hot_restart_version ...
  ADMIN_HOT_RESTART_VERSION=$(curl -sg http://${ADMIN_ADDRESS_0}/hot_restart_version)
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --max-obj-name-len 500 2>&1)
  check [ "${ADMIN_HOT_RESTART_VERSION}" = "${CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for hot-restart-version mismatch when max-obj-name-len differs
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --max-obj-name-len 1234 2>&1)
  check [ "${ADMIN_HOT_RESTART_VERSION}" != "${CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for hot-start-version mismatch when max-stats differs
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --max-stats 12345 2>&1)
  check [ "${ADMIN_HOT_RESTART_VERSION}" != "${CLI_HOT_RESTART_VERSION}" ]

  enableHeapCheck

  start_test Starting epoch 1
  ADMIN_ADDRESS_PATH_1="${TEST_TMPDIR}"/admin.1."${TEST_INDEX}".address
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 1 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --max-obj-name-len 500 --admin-address-path "${ADMIN_ADDRESS_PATH_1}"

  SECOND_SERVER_PID=$BACKGROUND_PID

  # Wait for stat flushing
  sleep 7

  start_test Checking that listener addresses have not changed
  HOT_RESTART_JSON_1="${TEST_TMPDIR}"/hot_restart.1."${TEST_INDEX}".yaml
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_1}" \
    "-u" "${HOT_RESTART_JSON_1}"
  CONFIG_DIFF=$(diff "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_1}")
  [[ -z "${CONFIG_DIFF}" ]]

  ADMIN_ADDRESS_PATH_2="${TEST_TMPDIR}"/admin.2."${TEST_INDEX}".address
  start_test Starting epoch 2
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 2  --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --max-obj-name-len 500 --admin-address-path "${ADMIN_ADDRESS_PATH_2}"

  THIRD_SERVER_PID=$BACKGROUND_PID
  sleep 3

  start_test Checking that listener addresses have not changed
  HOT_RESTART_JSON_2="${TEST_TMPDIR}"/hot_restart.2."${TEST_INDEX}".yaml
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_2}" \
    "-u" "${HOT_RESTART_JSON_2}"
  CONFIG_DIFF=$(diff "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_2}")
  [[ -z "${CONFIG_DIFF}" ]]

  # First server should already be gone.
  start_test Waiting for epoch 0
  wait ${FIRST_SERVER_PID}
  [[ $? == 0 ]]

  #Send SIGUSR1 signal to the second server, this should not kill it
  start_test Sending SIGUSR1 to the second server
  kill -SIGUSR1 ${SECOND_SERVER_PID}
  sleep 3

  # Now term the last server, and the other one should exit also.
  start_test Killing and waiting for epoch 2
  kill ${THIRD_SERVER_PID}
  wait ${THIRD_SERVER_PID}
  [[ $? == 0 ]]

  start_test Waiting for epoch 1
  wait ${SECOND_SERVER_PID}
  [[ $? == 0 ]]
  TEST_INDEX=$((TEST_INDEX+1))
done

# set -e forces the script to exit on non-zero exit codes. Set +e makes it easier to
# catch the non-zero exit code.
set +e
disableHeapCheck

start_test Launching envoy with no parameters. Check the exit value is 1
${ENVOY_BIN} --base_id "${BASE_ID}"
EXIT_CODE=$?
# The test should fail if the Envoy binary exits with anything other than 1.
if [[ $EXIT_CODE -ne 1 ]]; then
    echo "Envoy exited with code: ${EXIT_CODE}"
    exit 1
fi

enableHeapCheck
set -e

start_test disabling hot_restart by command line.
CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --disable-hot-restart 2>&1)
check [ "disabled" = "${CLI_HOT_RESTART_VERSION}" ]

echo "PASS"
