#!/bin/bash

set -e

# $1=<CA name>
generate_ca() {
  openssl genrsa -out $1key.pem 1024
  openssl req -new -key $1key.pem -out $1cert.csr -config $1cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cert.csr -signkey $1key.pem -out $1cert.pem \
    -extensions v3_ca -extfile $1cert.cfg
}

# $1=<certificate name> $2=<CA name>
generate_cert_key_pair() {
  openssl genrsa -out $1key.pem 1024
  openssl req -new -key $1key.pem -out $1cert.csr -config $1cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cert.csr -sha256 -CA $2cert.pem -CAkey \
    $2key.pem -CAcreateserial -out $1cert.pem -extensions v3_ca -extfile $1cert.cfg
  echo -e "// NOLINT(namespace-envoy)\n#define TEST_$(echo $1 | tr a-z A-Z)_CERT_HASH \"$(openssl x509 -in $1cert.pem -noout -fingerprint -sha256 | cut -d"=" -f2)\"" > $1cert_hash.h
}

# Generate cert for the CA.
generate_ca ca
# Generate cert for the server.
generate_cert_key_pair client ca
# Generate cert for the client.
generate_cert_key_pair server ca

# Generate cert for the upstream CA.
generate_ca upstreamca
# Generate cert for the upstream node.
generate_cert_key_pair upstream upstreamca

rm *.csr
rm *.srl
