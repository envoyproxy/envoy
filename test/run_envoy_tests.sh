#!/bin/bash

set -e

SOURCE_DIR=$1
BINARY_DIR=$2

# Create a test certificate with a 15-day expiration for SSL tests
TEST_CERT_DIR=/tmp/envoy_test
mkdir -p $TEST_CERT_DIR
openssl genrsa -out $TEST_CERT_DIR/unittestkey.pem 1024
openssl genrsa -out $TEST_CERT_DIR/unittestkey_expired.pem 1024
openssl req -new -key $TEST_CERT_DIR/unittestkey.pem -out $TEST_CERT_DIR/unittestcert.csr \
    -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Unit Test CA
unittest@lyft.com


EOF
openssl req -new -key $TEST_CERT_DIR/unittestkey_expired.pem -out $TEST_CERT_DIR/unittestcert_expired.csr \
    -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Unit Test CA
unittest@lyft.com


EOF
openssl x509 -req -days 15 -in $TEST_CERT_DIR/unittestcert.csr -sha256 -signkey \
    $TEST_CERT_DIR/unittestkey.pem -out $TEST_CERT_DIR/unittestcert.pem
openssl x509 -req -days -365 -in $TEST_CERT_DIR/unittestcert_expired.csr -sha256 -signkey \
    $TEST_CERT_DIR/unittestkey_expired.pem -out $TEST_CERT_DIR/unittestcert_expired.pem

# First run the normal unit test suite
cd $SOURCE_DIR
$BINARY_DIR/test/envoy-test

# Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
# sanity test.
echo "Starting epoch 0"
$BINARY_DIR/source/exe/envoy -c test/config/integration/server.json \
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
$BINARY_DIR/source/exe/envoy -c test/config/integration/server.json \
    --restart-epoch 1 --base-id 1 --service-cluster cluster --service-node node &

SECOND_SERVER_PID=$!
# Wait for stat flushing
sleep 7

echo "Starting epoch 2"
$BINARY_DIR/source/exe/envoy -c test/config/integration/server.json \
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

