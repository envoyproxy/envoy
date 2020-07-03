#!/bin/bash
#
# Create test certificates and OCSP responses for them for unittests.

set -e

trap cleanup EXIT
cleanup() {
  rm *_index*
  rm *.csr
  rm *.cnf
  rm *_serial*
}

[[ -z "${TEST_TMPDIR}" ]] && TEST_TMPDIR="$(cd $(dirname $0) && pwd)"

TEST_OCSP_DIR="${TEST_TMPDIR}/ocsp_test_data"
mkdir -p "${TEST_OCSP_DIR}"

cd $TEST_OCSP_DIR

##################################################
# Make the configuration file
##################################################

# $1=<certificate name> $2=<CA name>
generate_config() {
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
EOF
) > $1.cnf
}

# $1=<CA name> $2=[issuer name]
generate_ca() {
  if [[ "$2" != "" ]]; then local EXTRA_ARGS="-CA $2_cert.pem -CAkey $2_key.pem -CAcreateserial"; fi
  openssl genrsa -out $1_key.pem 2048
  openssl req -new -key $1_key.pem -out $1_cert.csr \
    -config $1.cnf -batch -sha256
  openssl x509 -req \
    -in $1_cert.csr -signkey $1_key.pem -out $1_cert.pem \
    -extensions v3_ca -extfile $1.cnf $EXTRA_ARGS
}

# $1=<certificate name> $2=<CA name>
generate_x509_cert() {
  openssl genrsa -out $1_key.pem 2048
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1.cnf -batch -sha256
  openssl ca -config $1.cnf -notext -batch -in $1_cert.csr -out $1_cert.pem
}

# $1=<certificate name> $2=<CA name> $3=<test name> $4=[extra args]
generate_ocsp_response() {
  # Generate an OCSP request
  openssl ocsp -CAfile $2_cert.pem -issuer $2_cert.pem \
    -cert $1_cert.pem -reqout $3_ocsp_req.der

  # Generate the OCSP response
  openssl ocsp -CA $2_cert.pem \
    -rkey $2_key.pem -rsigner $2_cert.pem -index $2_index.txt \
    -reqin $3_ocsp_req.der -respout $3_ocsp_resp.der $4
}

# $1=<certificate name> $2=<CA name>
revoke_certificate() {
  openssl ca -revoke $1_cert.pem -keyfile $2_key.pem -cert $2_cert.pem -config $2.cnf
}

# Set up the CA
touch ca_index.txt
echo "unique_subject = no" > ca_index.txt.attr
echo 1000 > ca_serial
generate_config ca ca
generate_ca ca

# Set up an intermediate CA with a different database
touch intermediate_ca_index.txt
echo "unique_subject = no" > intermediate_ca_index.txt.attr
echo 1000 > intermediate_ca_serial
generate_config intermediate_ca intermediate_ca
generate_ca intermediate_ca ca

# Generate valid cert and OCSP response
generate_config good ca
generate_x509_cert good ca
generate_ocsp_response good ca good "-ndays 7"

# Generate OCSP response with the responder key hash instead of name
generate_ocsp_response good ca responder_key_hash -resp_key_id

# Generate and revoke a cert and create OCSP response
generate_config revoked ca
generate_x509_cert revoked ca
revoke_certificate revoked ca
generate_ocsp_response revoked ca revoked

# Create OCSP response for cert unknown to the CA
generate_ocsp_response good intermediate_ca unknown

# Generate an OCSP request/response for multiple certs
openssl ocsp -CAfile ca_cert.pem -issuer ca_cert.pem \
  -cert good_cert.pem -cert revoked_cert.pem -reqout multiple_cert_ocsp_req.der
openssl ocsp -CA ca_cert.pem \
  -rkey ca_key.pem -rsigner ca_cert.pem -index ca_index.txt \
  -reqin multiple_cert_ocsp_req.der -respout multiple_cert_ocsp_resp.der
