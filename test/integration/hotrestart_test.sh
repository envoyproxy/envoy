#!/bin/bash

source "$TEST_SRCDIR/envoy/test/integration/test_utility.sh"

# TODO(htuch): In this test script, we are duplicating work done in test_environment.cc via sed.
# Instead, we can add a simple C++ binary that links against test_environment.cc and uses the
# substitution methods provided there.
JSON_TEST_ARRAY=()

# Ensure that the runtime watch root exist.
mkdir -p "${TEST_TMPDIR}"/test/common/runtime/test_data/current/envoy
mkdir -p "${TEST_TMPDIR}"/test/common/runtime/test_data/current/envoy_override

# Parameterize IPv4 and IPv6 testing.
if [[ -z "${ENVOY_IP_TEST_VERSIONS}" ]] || [[ "${ENVOY_IP_TEST_VERSIONS}" == "all" ]] \
  || [[ "${ENVOY_IP_TEST_VERSIONS}" == "v4only" ]]; then
  HOT_RESTART_JSON_V4="${TEST_TMPDIR}"/hot_restart_v4.yaml
  echo building ${HOT_RESTART_JSON_V4} ...
  cat "${TEST_SRCDIR}/envoy"/test/config/integration/server.yaml |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_SRCDIR/envoy#" | \
    sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
    sed -e "s#{{ reuse_port }}#false#" | \
    sed -e "s#{{ dns_lookup_family }}#V4_ONLY#" | \
    cat > "${HOT_RESTART_JSON_V4}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V4}")
fi

if [[ -z "${ENVOY_IP_TEST_VERSIONS}" ]] || [[ "${ENVOY_IP_TEST_VERSIONS}" == "all" ]] \
  || [[ "${ENVOY_IP_TEST_VERSIONS}" == "v6only" ]]; then
  HOT_RESTART_JSON_V6="${TEST_TMPDIR}"/hot_restart_v6.yaml
  cat "${TEST_SRCDIR}/envoy"/test/config/integration/server.yaml |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_SRCDIR/envoy#" | \
    sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#::1#" | \
    sed -e "s#{{ reuse_port }}#false#" | \
    sed -e "s#{{ dns_lookup_family }}#v6_only#" | \
    cat > "${HOT_RESTART_JSON_V6}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V6}")
fi

# Also test for listening on UNIX domain sockets. We use IPv4 for the
# upstreams to avoid too much wild sedding.
HOT_RESTART_JSON_UDS="${TEST_TMPDIR}"/hot_restart_uds.yaml
SOCKET_DIR="$(mktemp -d /tmp/envoy_test_hotrestart.XXXXXX)"
cat "${TEST_SRCDIR}/envoy"/test/config/integration/server_unix_listener.yaml |
  sed -e "s#{{ socket_dir }}#${SOCKET_DIR}#" | \
  sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
  cat > "${HOT_RESTART_JSON_UDS}"
JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_UDS}")

# Test reuse port listener.
HOT_RESTART_JSON_REUSE_PORT="${TEST_TMPDIR}"/hot_restart_v4.yaml
echo building ${HOT_RESTART_JSON_V4} ...
cat "${TEST_SRCDIR}/envoy"/test/config/integration/server.yaml |
  sed -e "s#{{ upstream_. }}#0#g" | \
  sed -e "s#{{ test_rundir }}#$TEST_SRCDIR/envoy#" | \
  sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#" | \
  sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
  sed -e "s#{{ reuse_port }}#true#" | \
  sed -e "s#{{ dns_lookup_family }}#V4_ONLY#" | \
  cat > "${HOT_RESTART_JSON_REUSE_PORT}"
JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_REUSE_PORT}")

# Enable this test to work with --runs_per_test
if [[ -z "${TEST_RANDOM_SEED}" ]]; then
  BASE_ID=1
else
  BASE_ID="${TEST_RANDOM_SEED}"
fi

echo "Hot restart test using --base-id ${BASE_ID}"

TEST_INDEX=0
function run_testsuite() {
  local HOT_RESTART_JSON="$1"
  local FAKE_SYMBOL_TABLE="$2"

  # TODO(jun03): instead of setting the base-id, the validate server should use the nop hot restart
  start_test validation
  check "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" --mode validate --service-cluster cluster \
      --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" --service-node node --base-id "${BASE_ID}"

  # Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
  # sanity test.
  start_test Starting epoch 0
  ADMIN_ADDRESS_PATH_0="${TEST_TMPDIR}"/admin.0."${TEST_INDEX}".address
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
      --restart-epoch 0 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" --admin-address-path "${ADMIN_ADDRESS_PATH_0}"

  FIRST_SERVER_PID=$BACKGROUND_PID

  start_test Updating original config listener addresses
  sleep 3

  UPDATED_HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart_updated."${TEST_INDEX}".yaml
  "${TEST_SRCDIR}/envoy"/tools/socket_passing "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_0}" \
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
  EXPECTED_CLI_HOT_RESTART_VERSION="11.104"
  echo "The Envoy's hot restart version is ${CLI_HOT_RESTART_VERSION}"
  echo "Now checking that the above version is what we expected."
  check [ "${CLI_HOT_RESTART_VERSION}" = "${EXPECTED_CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for consistency of /hot_restart_version with --use-fake-symbol-table "$FAKE_SYMBOL_TABLE"
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" 2>&1)
  EXPECTED_CLI_HOT_RESTART_VERSION="11.104"
  check [ "${CLI_HOT_RESTART_VERSION}" = "${EXPECTED_CLI_HOT_RESTART_VERSION}" ]

  start_test Checking for match of --hot-restart-version and admin /hot_restart_version
  ADMIN_ADDRESS_0=$(cat "${ADMIN_ADDRESS_PATH_0}")
  echo fetching hot restart version from http://${ADMIN_ADDRESS_0}/hot_restart_version ...
  ADMIN_HOT_RESTART_VERSION=$(curl -sg http://${ADMIN_ADDRESS_0}/hot_restart_version)
  echo "Fetched ADMIN_HOT_RESTART_VERSION is ${ADMIN_HOT_RESTART_VERSION}"
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" \
    --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" 2>&1)
  check [ "${ADMIN_HOT_RESTART_VERSION}" = "${CLI_HOT_RESTART_VERSION}" ]

  start_test Checking server.hot_restart_generation 1
  GENERATION_0=$(curl -sg http://${ADMIN_ADDRESS_0}/stats | grep server.hot_restart_generation)
  check [ "$GENERATION_0" = "server.hot_restart_generation: 1" ];

  # Verify we can see server.live in the admin port.
  SERVER_LIVE_0=$(curl -sg http://${ADMIN_ADDRESS_0}/stats | grep server.live)
  check [ "$SERVER_LIVE_0" = "server.live: 1" ];

  enableHeapCheck

  start_test Starting epoch 1
  ADMIN_ADDRESS_PATH_1="${TEST_TMPDIR}"/admin.1."${TEST_INDEX}".address
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 1 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" --admin-address-path "${ADMIN_ADDRESS_PATH_1}"

  SECOND_SERVER_PID=$BACKGROUND_PID

  # Wait for stat flushing
  sleep 7

  ADMIN_ADDRESS_1=$(cat "${ADMIN_ADDRESS_PATH_1}")
  SERVER_LIVE_1=$(curl -sg http://${ADMIN_ADDRESS_1}/stats | grep server.live)
  check [ "$SERVER_LIVE_1" = "server.live: 1" ];

  start_test Checking that listener addresses have not changed
  HOT_RESTART_JSON_1="${TEST_TMPDIR}"/hot_restart.1."${TEST_INDEX}".yaml
  "${TEST_SRCDIR}/envoy"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_1}" \
    "-u" "${HOT_RESTART_JSON_1}"
  CONFIG_DIFF=$(diff "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_1}")
  [[ -z "${CONFIG_DIFF}" ]]

  start_test Checking server.hot_restart_generation 2
  GENERATION_1=$(curl -sg http://${ADMIN_ADDRESS_1}/stats | grep server.hot_restart_generation)
  check [ "$GENERATION_1" = "server.hot_restart_generation: 2" ];

  ADMIN_ADDRESS_PATH_2="${TEST_TMPDIR}"/admin.2."${TEST_INDEX}".address
  start_test Starting epoch 2
  run_in_background_saving_pid "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 2  --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --use-fake-symbol-table "$FAKE_SYMBOL_TABLE" --admin-address-path "${ADMIN_ADDRESS_PATH_2}"

  THIRD_SERVER_PID=$BACKGROUND_PID
  sleep 3

  start_test Checking that listener addresses have not changed
  HOT_RESTART_JSON_2="${TEST_TMPDIR}"/hot_restart.2."${TEST_INDEX}".yaml
  "${TEST_SRCDIR}/envoy"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_2}" \
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
}

for HOT_RESTART_JSON in "${JSON_TEST_ARRAY[@]}"
do
  # Run one of the tests with real symbol tables. No need to do all of them.
  if [ "$TEST_INDEX" = "0" ]; then
    run_testsuite "$HOT_RESTART_JSON" "0" || exit 1
  fi

  run_testsuite "$HOT_RESTART_JSON" "1" || exit 1
done

start_test disabling hot_restart by command line.
CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --disable-hot-restart 2>&1)
check [ "disabled" = "${CLI_HOT_RESTART_VERSION}" ]

echo "PASS"
