#!/bin/bash
#
# Create a test certificate with a 15-day expiration for SSL tests

set -e

TEST_CERT_DIR=$TEST_TMPDIR

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
