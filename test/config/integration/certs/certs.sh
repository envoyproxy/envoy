#!/bin/bash

set -e

# $1=<CA name> $2=[issuer name]
generate_ca() {
  local extra_args=()
  if [[ -n "$2" ]]; then
      extra_args=(-CA "${2}cert.pem" -CAkey "${2}key.pem" -CAcreateserial);
  else
      extra_args=(-signkey "${1}key.pem");
  fi
  openssl genrsa -out "${1}key.pem" 2048
  openssl req -new -key "${1}key.pem" -out "${1}cert.csr" -config "${1}cert.cfg" -batch -sha256
  openssl x509 -req -days 730 -in "${1}cert.csr" -out "${1}cert.pem" \
    -extensions v3_ca -extfile "${1}cert.cfg" "${extra_args[@]}"
  generate_info_header "$1"
}

# $1=<certificate name>
generate_rsa_key() {
  openssl genrsa -out "${1}key.pem" 2048
}

# $1=<certificate name>
generate_ecdsa_key() {
  openssl ecparam -name secp256r1 -genkey -out "${1}key.pem"
}

# $1=<certificate name> $2=<CA name> $3=[days]
generate_x509_cert() {
  local days="${3:-730}"
  openssl req -new -key "${1}key.pem" -out "${1}cert.csr" -config "${1}cert.cfg" -batch -sha256
  openssl x509 -req -days "${days}" -in "${1}cert.csr" -sha256 -CA "${2}cert.pem" -CAkey \
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

# $1=<certificate name>
generate_info_header() {
    local prefix
    prefix="TEST_$(echo "$1" | tr '[:lower:]' '[:upper:]')"
    {
        echo "// NOLINT(namespace-envoy)"
        echo "constexpr char ${prefix}_CERT_256_HASH[] ="
        echo "    \"$(openssl x509 -in "${1}cert.pem" -outform DER | openssl dgst -sha256 | cut -d" " -f2)\";"
        echo "constexpr char ${prefix}_CERT_1_HASH[] = \"$(openssl x509 -in "${1}cert.pem" -outform DER | openssl dgst -sha1 | cut -d" " -f2)\";"
        echo "constexpr char ${prefix}_CERT_SPKI[] = \"$(openssl x509 -in "${1}cert.pem" -noout -pubkey | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl enc -base64)\";"
        echo "constexpr char ${prefix}_CERT_SERIAL[] = \"$(openssl x509 -in "${1}cert.pem" -noout -serial | cut -d"=" -f2 | awk '{print tolower($0)}')\";"
    } > "${1}cert_info.h"
}

# Generate cert for the CA.
generate_ca ca
# Generate intermediate_ca_cert.pem.
generate_ca intermediate_ca ca
# Generate 2nd intermediate ca.
generate_ca intermediate_ca_2 intermediate_ca
# Concatenate intermediate and ca certs create valid certificate chain.
cat cacert.pem intermediate_cacert.pem  intermediate_ca_2cert.pem > intermediate_ca_cert_chain.pem
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
generate_rsa_key client
generate_x509_cert client ca
# Generate cert for the client, signed by Intermediate CA.
generate_rsa_key client2 ca
generate_x509_cert client2 intermediate_ca_2
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

# Generate expired_cert.pem as a self-signed, expired cert (will fail on macOS 10.13+ because of negative days value).
generate_rsa_key expired_
generate_x509_cert expired_ ca -365

rm ./*.csr
rm ./*.srl
