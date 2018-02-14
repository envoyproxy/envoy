#!/bin/bash

set -e

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
  HOT_RESTART_JSON_V4="${TEST_TMPDIR}"/hot_restart_v4.json
  cat "${TEST_RUNDIR}"/test/config/integration/server.json |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_RUNDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#127.0.0.1#" | \
    sed -e "s#{{ dns_lookup_family }}#v4_only#" | \
    cat > "${HOT_RESTART_JSON_V4}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V4}")
fi

if [[ -z "${ENVOY_IP_TEST_VERSIONS}" ]] || [[ "${ENVOY_IP_TEST_VERSIONS}" == "all" ]] \
  || [[ "${ENVOY_IP_TEST_VERSIONS}" == "v6only" ]]; then
  HOT_RESTART_JSON_V6="${TEST_TMPDIR}"/hot_restart_v6.json
  cat "${TEST_RUNDIR}"/test/config/integration/server.json |
    sed -e "s#{{ upstream_. }}#0#g" | \
    sed -e "s#{{ test_rundir }}#$TEST_RUNDIR#" | \
    sed -e "s#{{ ip_loopback_address }}#[::1]#" | \
    sed -e "s#{{ dns_lookup_family }}#v6_only#" | \
    cat > "${HOT_RESTART_JSON_V6}"
  JSON_TEST_ARRAY+=("${HOT_RESTART_JSON_V6}")
fi

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
  # Test validation.
  # TODO(jun03): instead of setting the base-id, the validate server should use the nop hot restart
  "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" --mode validate --service-cluster cluster \
      --service-node node --base-id "${BASE_ID}"

  # Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
  # sanity test.
  echo "Starting epoch 0"
  ADMIN_ADDRESS_PATH_0="${TEST_TMPDIR}"/admin.0."${TEST_INDEX}".address
  "${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
      --restart-epoch 0 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --admin-address-path "${ADMIN_ADDRESS_PATH_0}" &

  FIRST_SERVER_PID=$!
  sleep 3

  echo "Updating original config json listener addresses"
  UPDATED_HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart_updated."${TEST_INDEX}".json
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_0}" \
    "-u" "${UPDATED_HOT_RESTART_JSON}"

  # Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
  # get eaten.
  echo "Sending SIGUSR1/SIGHUP to first server"
  kill -SIGUSR1 ${FIRST_SERVER_PID}
  kill -SIGHUP ${FIRST_SERVER_PID}
  sleep 3

  disableHeapCheck

  echo "Checking for match of --hot-restart-version and admin /hot_restart_version"
  ADMIN_ADDRESS_0=$(cat "${ADMIN_ADDRESS_PATH_0}")
  ADMIN_HOT_RESTART_VERSION=$(curl -sg http://${ADMIN_ADDRESS_0}/hot_restart_version)
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --base-id "${BASE_ID}" 2>&1)
  if [[ "${ADMIN_HOT_RESTART_VERSION}" != "${CLI_HOT_RESTART_VERSION}" ]]; then
      echo "Hot restart version mismatch: ${ADMIN_HOT_RESTART_VERSION} != " \
           "${CLI_HOT_RESTART_VERSION}"
      exit 2
  fi

  echo "Checking max-obj-name-len"
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --max-obj-name-len 1234 --base-id "${BASE_ID}" 2>&1)
  if [[ "${ADMIN_HOT_RESTART_VERSION}" = "${CLI_HOT_RESTART_VERSION}" ]]; then
      echo "Hot restart version match when it should mismatch: ${ADMIN_HOT_RESTART_VERSION} == " \
           "${CLI_HOT_RESTART_VERSION}"
      exit 2
  fi

  echo "Checking max-stats"
  CLI_HOT_RESTART_VERSION=$("${ENVOY_BIN}" --hot-restart-version --max-stats 12345 --base-id "${BASE_ID}" 2>&1)
  if [[ "${ADMIN_HOT_RESTART_VERSION}" = "${CLI_HOT_RESTART_VERSION}" ]]; then
      echo "Hot restart version match when it should mismatch: ${ADMIN_HOT_RESTART_VERSION} == " \
           "${CLI_HOT_RESTART_VERSION}"
      exit 2
  fi

  enableHeapCheck

  echo "Starting epoch 1"
  ADMIN_ADDRESS_PATH_1="${TEST_TMPDIR}"/admin.1."${TEST_INDEX}".address
  "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 1 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --admin-address-path "${ADMIN_ADDRESS_PATH_1}" &

  SECOND_SERVER_PID=$!
  # Wait for stat flushing
  sleep 7

  echo "Checking that listener addresses have not changed"
  HOT_RESTART_JSON_1="${TEST_TMPDIR}"/hot_restart.1."${TEST_INDEX}".json
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_1}" \
    "-u" "${HOT_RESTART_JSON_1}"
  CONFIG_DIFF=$(diff "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_1}")
  [[ -z "${CONFIG_DIFF}" ]]

  ADMIN_ADDRESS_PATH_2="${TEST_TMPDIR}"/admin.2."${TEST_INDEX}".address
  echo "Starting epoch 2"
  "${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
      --restart-epoch 2 --base-id "${BASE_ID}" --service-cluster cluster --service-node node \
      --admin-address-path "${ADMIN_ADDRESS_PATH_2}" &

  THIRD_SERVER_PID=$!
  sleep 3

  echo "Checking that listener addresses have not changed"
  HOT_RESTART_JSON_2="${TEST_TMPDIR}"/hot_restart.2."${TEST_INDEX}".json
  "${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_2}" \
    "-u" "${HOT_RESTART_JSON_2}"
  CONFIG_DIFF=$(diff "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_2}")
  [[ -z "${CONFIG_DIFF}" ]]

  # First server should already be gone.
  echo "Waiting for epoch 0"
  wait ${FIRST_SERVER_PID}
  [[ $? == 0 ]]

  #Send SIGUSR1 signal to the second server, this should not kill it
  echo "Sending SIGUSR1 to the second server"
  kill -SIGUSR1 ${SECOND_SERVER_PID}
  sleep 3

  # Now term the last server, and the other one should exit also.
  echo "Killing and waiting for epoch 2"
  kill ${THIRD_SERVER_PID}
  wait ${THIRD_SERVER_PID}
  [[ $? == 0 ]]

  echo "Waiting for epoch 1"
  wait ${SECOND_SERVER_PID}
  [[ $? == 0 ]]
  TEST_INDEX=$((TEST_INDEX+1))
done

# set -e forces the script to exit on non-zero exit codes. Set +e makes it easier to
# catch the non-zero exit code.
set +e
disableHeapCheck

echo "Launching envoy with no parameters. Check the exit value is 1"
${ENVOY_BIN} --base_id "${BASE_ID}"
EXIT_CODE=$?
# The test should fail if the Envoy binary exits with anything other than 1.
if [[ $EXIT_CODE -ne 1 ]]; then
    echo "Envoy exited with code: ${EXIT_CODE}"
    exit 1
fi

enableHeapCheck
set -e

echo "PASS"
