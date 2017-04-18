#!/bin/bash

set -e

[[ -z "${ENVOY_BIN}" ]] && ENVOY_BIN=source/exe/envoy-static

# TODO(htuch): Clean this up when Bazelifying the hot restart test below. At the same time, restore
# some test behavior lost in #650, when we switched to 0 port binding - the hot restart tests no
# longer check socket passing. See #654.
HOT_RESTART_JSON="${TEST_TMPDIR}"/hot_restart.json
cat test/config/integration/server.json |
  sed -e "s#{{ upstream_. }}#0#g" | \
  cat > "${HOT_RESTART_JSON}"

# Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
# sanity test.
ADMIN_ADDRESS_PATH_0="${TEST_TMPDIR}"/admin_address_file_0.pid
echo "Starting epoch 0"
"${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
    --restart-epoch 0 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_0}" &

FIRST_SERVER_PID=$!
sleep 3

echo "Replacing listener addresses"
"tools/socket_passing.py" "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_0}"

# Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
# get eaten.
echo "Sending SIGUSR1/SIGHUP to first server"
kill -SIGUSR1 ${FIRST_SERVER_PID}
kill -SIGHUP ${FIRST_SERVER_PID}
sleep 3

ADMIN_ADDRESS_PATH_1="${TEST_TMPDIR}"/admin_address_file_1.pid
echo "Starting epoch 1"
"${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
    --restart-epoch 1 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_1}" &

SECOND_SERVER_PID=$!
# Wait for stat flushing
sleep 7

echo "Checking that listener addresses have not changed"
"tools/socket_passing.py" "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_1}" "-n"

ADMIN_ADDRESS_PATH_2="${TEST_TMPDIR}"/admin_address_file_2.pid
echo "Starting epoch 2"
"${ENVOY_BIN}" -c "${HOT_RESTART_JSON}" \
    --restart-epoch 2 --base-id 1 --service-cluster cluster --service-node node \
    -a "${ADMIN_ADDRESS_PATH_2}" &

THIRD_SERVER_PID=$!
sleep 3

echo "Checking that listener addresses have not changed"
"tools/socket_passing.py" "-o" "${HOT_RESTART_JSON}" "-a" "${ADMIN_ADDRESS_PATH_2}" "-n"

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
