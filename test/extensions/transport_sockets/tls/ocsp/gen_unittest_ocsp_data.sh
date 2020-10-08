#!/bin/bash
#
# Create test certificates and OCSP responses for them for unittests.

set -e

trap cleanup EXIT
cleanup() {
  rm -f ./*_index*
  rm -f ./*.csr
  rm -f ./*.cnf
  rm -f ./*_serial*
}

[[ -z "${TEST_TMPDIR}" ]] && TEST_TMPDIR="$(cd "$(dirname "$0")" && pwd)"

TEST_OCSP_DIR="${TEST_TMPDIR}/ocsp_test_data"
mkdir -p "${TEST_OCSP_DIR}"

rm -f "${TEST_OCSP_DIR}"/*

cd "$TEST_OCSP_DIR" || exit 1

##################################################
# Make the configuration file
##################################################

# $1=<certificate name> $2=<CA name>
generate_config() {
touch "${1}_index.txt"
echo "unique_subject = no" > "${1}_index.txt.attr"
echo 1000 > "${1}_serial"

(cat << EOF
[ req ]
default_bits            = 2048
distinguished_name      = req_distinguished_name

[ req_distinguished_name ]
countryName = US
countryName_default = US
stateOrProvinceName = California
stateOrProvinceName_default = California
localityName = San Francisco
localityName_default = San Francisco
organizationName = Lyft
organizationName_default = Lyft
organizationalUnitName = Lyft Engineering
organizationalUnitName_default = Lyft Engineering
commonName = $1
commonName_default = $1
commonName_max  = 64

[ ca ]
default_ca = CA_default

[ CA_default ]
dir           = ${TEST_OCSP_DIR}
certs         = ${TEST_OCSP_DIR}
new_certs_dir = ${TEST_OCSP_DIR}
serial        = ${TEST_OCSP_DIR}
database      = ${TEST_OCSP_DIR}/$2_index.txt
serial        = ${TEST_OCSP_DIR}/$2_serial

private_key   = ${TEST_OCSP_DIR}/$2_key.pem
certificate   = ${TEST_OCSP_DIR}/$2_cert.pem

default_days  = 375
default_md    = sha256
preserve      = no
policy        = policy_default

[ policy_default ]
countryName             = optional
stateOrProvinceName     = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional


[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, digitalSignature, cRLSign, keyCertSign

[ must_staple ]
tlsfeature = status_request
EOF
) > "${1}.cnf"
}

# $1=<CA name> $2=[issuer name]
generate_ca() {
  local extra_args=()
  if [[ -n "$2" ]]; then
      extra_args=(-CA "${2}_cert.pem" -CAkey "${2}_key.pem" -CAcreateserial)
  fi
  openssl genrsa -out "${1}_key.pem" 2048
  openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" \
    -config "${1}.cnf" -batch -sha256
  openssl x509 -req \
    -in "${1}_cert.csr" -signkey "${1}_key.pem" -out "${1}_cert.pem" \
    -extensions v3_ca -extfile "${1}.cnf" "${extra_args[@]}"
}

# $1=<certificate name> $2=<CA name> $3=[req args]
generate_rsa_cert() {
  openssl genrsa -out "${1}_key.pem" 2048
  openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}.cnf" -batch -sha256
  openssl ca -config "${1}.cnf" -notext -batch -in "${1}_cert.csr" -out "${1}_cert.pem" "${@:3}"
}

# $1=<certificate name> $2=<CA name> $3=[req args]
generate_ecdsa_cert() {
  openssl ecparam -name secp256r1 -genkey -out "${1}_key.pem"
  openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}.cnf" -batch -sha256
  openssl ca -config "${1}.cnf" -notext -batch -in "${1}_cert.csr" -out "${1}_cert.pem" "${@:3}"
}

# $1=<certificate name> $2=<CA name> $3=<test name> $4=[extra args]
generate_ocsp_response() {
  # Generate an OCSP request
  openssl ocsp -CAfile "${2}_cert.pem" -issuer "${2}_cert.pem" \
    -cert "${1}_cert.pem" -reqout "${3}_ocsp_req.der"

  # Generate the OCSP response
  openssl ocsp -CA "${2}_cert.pem" \
    -rkey "${2}_key.pem" -rsigner "${2}_cert.pem" -index "${2}_index.txt" \
    -reqin "${3}_ocsp_req.der" -respout "${3}_ocsp_resp.der" "${@:4}"
}

# $1=<certificate name> $2=<CA name>
revoke_certificate() {
  openssl ca -revoke "${1}_cert.pem" -keyfile "${2}_key.pem" -cert "${2}_cert.pem" -config "${2}.cnf"
}

# $1=<test name> $2=<CA name>
dump_ocsp_details() {
  openssl ocsp -respin "${1}_ocsp_resp.der" -issuer "${2}_cert.pem" -resp_text \
    -out "${1}_ocsp_resp_details.txt"
}

# Set up the CA
generate_config ca ca
generate_ca ca

# Set up an intermediate CA with a different database
generate_config intermediate_ca intermediate_ca
generate_ca intermediate_ca ca

# Generate valid cert and OCSP response
generate_config good ca
generate_rsa_cert good ca
generate_ocsp_response good ca good -ndays 7
dump_ocsp_details good ca

# Generate OCSP response with the responder key hash instead of name
generate_ocsp_response good ca responder_key_hash -resp_key_id

# Generate and revoke a cert and create OCSP response
generate_config revoked ca
generate_rsa_cert revoked ca -extensions must_staple
revoke_certificate revoked ca
generate_ocsp_response revoked ca revoked

# Create OCSP response for cert unknown to the CA
generate_ocsp_response good intermediate_ca unknown

# Generate cert with ECDSA key and OCSP response
generate_config ecdsa ca
generate_ecdsa_cert ecdsa ca
generate_ocsp_response ecdsa ca ecdsa

# Generate an OCSP request/response for multiple certs
openssl ocsp -CAfile ca_cert.pem -issuer ca_cert.pem \
  -cert good_cert.pem -cert revoked_cert.pem -reqout multiple_cert_ocsp_req.der
openssl ocsp -CA ca_cert.pem \
  -rkey ca_key.pem -rsigner ca_cert.pem -index ca_index.txt \
  -reqin multiple_cert_ocsp_req.der -respout multiple_cert_ocsp_resp.der
