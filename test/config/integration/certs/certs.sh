#!/bin/bash

set -e

generate_ca() {
  openssl genrsa -out cakey.pem 1024
  openssl req -new -key $1cakey.pem -out $1cacert.csr -config $1cacert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cacert.csr -signkey $1cakey.pem -out $1cacert.pem \
    -extensions v3_ca -extfile $1cacert.cfg
}

generate_cert_key_pair() {
  openssl genrsa -out $1key.pem 1024
  openssl req -new -key $1key.pem -out $1cert.csr -config $1cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cert.csr -sha256 -CA $2cacert.pem -CAkey \
    $2cakey.pem -CAcreateserial -out $1cert.pem -extensions v3_ca -extfile $1cert.cfg
  echo -e "// NOLINT(namespace-envoy)\n#define TEST_$(echo $1 | tr a-z A-Z)_CERT_HASH \"$(openssl x509 -in $1cert.pem -noout -fingerprint -sha256 | cut -d"=" -f2)\"" > $1cert_hash.h
}

# Generate cert for the CA.
generate_ca
# Generate cert for the server.
generate_cert_key_pair client
# Generate cert for the client.
generate_cert_key_pair server

# Generate cert for the upstream CA.
generate_ca upstream
# Generate cert for the upstream node.
generate_cert_key_pair upstream upstream

rm *.csr
rm *.srl
