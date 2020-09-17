#!/bin/bash

set -e

# $1=<CA name>
generate_ca() {
  openssl genrsa -out "${1}key.pem" 2048
  openssl req -new -key "${1}key.pem" -out "${1}cert.csr" -config "${1}cert.cfg" -batch -sha256
  openssl x509 -req -days 730 -in "${1}cert.csr" -signkey "${1}key.pem" -out "${1}cert.pem" \
    -extensions v3_ca -extfile "${1}cert.cfg"
}

# $1=<certificate name>
generate_rsa_key() {
  openssl genrsa -out "${1}key.pem" 2048
}

# $1=<certificate name>
generate_ecdsa_key() {
  openssl ecparam -name secp256r1 -genkey -out "${1}key.pem"
}

# $1=<certificate name> $2=<CA name>
generate_x509_cert() {
  openssl req -new -key "${1}key.pem" -out "${1}cert.csr" -config "${1}cert.cfg" -batch -sha256
  openssl x509 -req -days 730 -in "${1}cert.csr" -sha256 -CA "${2}cert.pem" -CAkey \
    "${2}key.pem" -CAcreateserial -out "${1}cert.pem" -extensions v3_ca -extfile "${1}cert.cfg"
  echo -e "// NOLINT(namespace-envoy)\nconstexpr char TEST_$(echo "$1" | tr "[:lower:]" "[:upper:]")_CERT_HASH[] = \"$(openssl x509 -in "${1}cert.pem" -noout -fingerprint -sha256 | cut -d"=" -f2)\";" > "${1}cert_hash.h"
}

# $1=<certificate name> $2=<CA name>
generate_ocsp_response() {
  # Generate an OCSP request
  openssl ocsp -CAfile "${2}cert.pem" -issuer "${2}cert.pem" \
    -cert "${1}cert.pem" -reqout "${1}_ocsp_req.der"

  # Generate the OCSP response
  # Note: A database of certs is necessary to generate ocsp
  # responses with `openssl ocsp`. `generate_x509_cert` does not use one
  # so we must create an empty one here. Since generated certs are not
  # tracked in this index, all ocsp response will have a cert status
  # "unknown", but are still valid responses and the cert status should
  # not matter for integration tests
  touch "${2}_index.txt"
  openssl ocsp -CA "${2}cert.pem" \
    -rkey "${2}key.pem" -rsigner "${2}cert.pem" -index "${2}_index.txt" \
    -reqin "${1}_ocsp_req.der" -respout "${1}_ocsp_resp.der" -ndays 730

  rm "${1}_ocsp_req.der" "${2}_index.txt"
}

# Generate cert for the CA.
generate_ca ca
# Generate RSA cert for the server.
generate_rsa_key server ca
generate_x509_cert server ca
generate_ocsp_response server ca
# Generate ECDSA cert for the server.
cp -f servercert.cfg server_ecdsacert.cfg
generate_ecdsa_key server_ecdsa ca
generate_x509_cert server_ecdsa ca
generate_ocsp_response server_ecdsa ca
rm -f server_ecdsacert.cfg
# Generate cert for the client.
generate_rsa_key client ca
generate_x509_cert client ca
# Generate ECDSA cert for the client.
cp -f clientcert.cfg client_ecdsacert.cfg
generate_ecdsa_key client_ecdsa ca
generate_x509_cert client_ecdsa ca
rm -f client_ecdsacert.cfg

# Generate cert for the upstream CA.
generate_ca upstreamca
# Generate cert for the upstream node.
generate_rsa_key upstream upstreamca
generate_x509_cert upstream upstreamca
generate_rsa_key upstreamlocalhost upstreamca
generate_x509_cert upstreamlocalhost upstreamca

rm ./*.csr
rm ./*.srl
