#!/bin/bash

set -e

# $1=<CA name>
generate_ca() {
  openssl genrsa -out $1key.pem 1024
  openssl req -new -key $1key.pem -out $1cert.csr -config $1cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cert.csr -signkey $1key.pem -out $1cert.pem \
    -extensions v3_ca -extfile $1cert.cfg
}

# $1=<certificate name>
generate_rsa_key() {
  openssl genrsa -out $1key.pem 1024
}

# $1=<certificate name>
generate_ecdsa_key() {
  openssl ecparam -name secp256r1 -genkey -out $1key.pem
}

# $1=<certificate name> $2=<CA name>
generate_x509_cert() {
  openssl req -new -key $1key.pem -out $1cert.csr -config $1cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1cert.csr -sha256 -CA $2cert.pem -CAkey \
    $2key.pem -CAcreateserial -out $1cert.pem -extensions v3_ca -extfile $1cert.cfg
  echo -e "// NOLINT(namespace-envoy)\nconstexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_HASH[] = \"$(openssl x509 -in $1cert.pem -noout -fingerprint -sha256 | cut -d"=" -f2)\";" > $1cert_hash.h
}

# Generate cert for the CA.
generate_ca ca
# Generate RSA cert for the server.
generate_rsa_key server ca
generate_x509_cert server ca
# Generate ECDSA cert for the server.
cp -f servercert.cfg server_ecdsacert.cfg
generate_ecdsa_key server_ecdsa ca
generate_x509_cert server_ecdsa ca
rm -f server_ecdsacert.cfg
# Generate cert for the client.
generate_rsa_key client ca
generate_x509_cert client ca

# Generate cert for the upstream CA.
generate_ca upstreamca
# Generate cert for the upstream node.
generate_rsa_key upstream upstreamca
generate_x509_cert upstream upstreamca

rm *.csr
rm *.srl
