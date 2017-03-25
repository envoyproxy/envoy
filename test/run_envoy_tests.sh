#!/bin/bash

set -e

SOURCE_DIR=$1
BINARY_DIR=$2

source $SOURCE_DIR/test/setup_test_env.sh

$SOURCE_DIR/test/certs/gen_test_certs.sh $TEST_SRCDIR/test/certs
$SOURCE_DIR/test/config/integration/gen_test_configs.sh $SOURCE_DIR/test/config/integration \
  $TEST_SRCDIR/test/config/integration

# First run the normal unit test suite
cd $SOURCE_DIR
$RUN_TEST_UNDER $BINARY_DIR/test/envoy-test $EXTRA_TEST_ARGS

if [ "$UNIT_TEST_ONLY" = "1" ]
then
  exit 0
fi

# Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
# sanity test.
echo "Starting epoch 0"
$BINARY_DIR/source/exe/envoy -c $TEST_SRCDIR/test/config/integration/server.json \
    --restart-epoch 0 --base-id 1 --service-cluster cluster --service-node node &

FIRST_SERVER_PID=$!
sleep 3
# Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
# get eaten.
echo "Sending SIGUSR1/SIGHUP to first server"
kill -SIGUSR1 $FIRST_SERVER_PID
kill -SIGHUP $FIRST_SERVER_PID
sleep 3

echo "Starting epoch 1"
$BINARY_DIR/source/exe/envoy -c $TEST_SRCDIR/test/config/integration/server.json \
    --restart-epoch 1 --base-id 1 --service-cluster cluster --service-node node &

SECOND_SERVER_PID=$!
# Wait for stat flushing
sleep 7

echo "Starting epoch 2"
$BINARY_DIR/source/exe/envoy -c $TEST_SRCDIR/test/config/integration/server.json \
    --restart-epoch 2 --base-id 1 --service-cluster cluster --service-node node &

THIRD_SERVER_PID=$!
sleep 3

# First server should already be gone.
echo "Waiting for epoch 0"
wait $FIRST_SERVER_PID
[[ $? == 0 ]]

#Send SIGUSR1 signal to the second server, this should not kill it
echo "Sending SIGUSR1 to the second server"
kill -SIGUSR1 $SECOND_SERVER_PID
sleep 3

# Now term the last server, and the other one should exit also.
echo "Killing and waiting for epoch 2"
kill $THIRD_SERVER_PID
wait $THIRD_SERVER_PID
[[ $? == 0 ]]

echo "Waiting for epoch 1"
wait $SECOND_SERVER_PID
[[ $? == 0 ]]

