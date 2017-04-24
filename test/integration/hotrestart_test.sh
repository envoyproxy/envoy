#!/bin/bash

set -e

[[ -z "${ENVOY_BIN}" ]] && ENVOY_BIN="${TEST_RUNDIR}"/source/exe/envoy-static

# TODO(htuch): Clean this up when Bazelifying the hot restart test below. At the same time, restore
# some test behavior lost in #650, when we switched to 0 port binding - the hot restart tests no
# longer check socket passing. See #654.
HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart.json
cat "${TEST_RUNDIR}"/test/config/integration/server.json |
  sed -e "s#{{ upstream_. }}#0#g" | \
  sed -e "s#{{ test_rundir }}#$TEST_RUNDIR#" | \
  cat > "${HOT_RESTART_JSON}"

# Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
# sanity test.
echo "Starting epoch 0"
ADMIN_ADDRESS_PATH_0="${TEST_TMPDIR}"/admin_0.address
"${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
    --restart-epoch 0 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_0}" &

FIRST_SERVER_PID=$!
sleep 3

echo "Updating original config json listener addresses"
UPDATED_HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart_updated.json
"${TEST_RUNDIR}"/tools/socket_passing "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_0}" \
  "-u" "${UPDATED_HOT_RESTART_JSON}"

# Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
# get eaten.
echo "Sending SIGUSR1/SIGHUP to first server"
kill -SIGUSR1 ${FIRST_SERVER_PID}
kill -SIGHUP ${FIRST_SERVER_PID}
sleep 3

echo "Starting epoch 1"
ADMIN_ADDRESS_PATH_1="${TEST_TMPDIR}"/admin_1.address
"${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
    --restart-epoch 1 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_1}" &

SECOND_SERVER_PID=$!
# Wait for stat flushing
sleep 7

echo "Checking that listener addresses have not changed"
HOT_RESTART_JSON_1="${TEST_TMPDIR}"/hot_restart_1.json
"${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_1}" \
  "-u" "${HOT_RESTART_JSON_1}"
CONFIG_DIFF=$(diff -Z "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_1}")
[[ -z "${CONFIG_DIFF}" ]]

ADMIN_ADDRESS_PATH_2="${TEST_TMPDIR}"/admin_2.address
echo "Starting epoch 2"
"${ENVOY_BIN}" -c "${UPDATED_HOT_RESTART_JSON}" \
    --restart-epoch 2 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_2}" &

THIRD_SERVER_PID=$!
sleep 3

echo "Checking that listener addresses have not changed"
HOT_RESTART_JSON_2="${TEST_TMPDIR}"/hot_restart_2.json
"${TEST_RUNDIR}"/tools/socket_passing "-o" "${UPDATED_HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_2}" \
  "-u" "${HOT_RESTART_JSON_2}"
CONFIG_DIFF=$(diff -Z "${UPDATED_HOT_RESTART_JSON}" "${HOT_RESTART_JSON_2}")
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

echo "PASS"
