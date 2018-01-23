#!/bin/bash
#
# Create a test certificate with a 15-day expiration for SSL tests.

set -e

TEST_CERT_DIR="${TEST_TMPDIR}"

mkdir -p "${TEST_CERT_DIR}"

export OPENSSL_CONF="${TEST_CERT_DIR}"/openssl.cnf
(cat << EOF
[ req ]
default_bits            = 2048
distinguished_name      = req_distinguished_name

[ req_distinguished_name ]
countryName                     = Country Name (2 letter code)
countryName_default             = AU
countryName_min                 = 2
countryName_max                 = 2

stateOrProvinceName             = State or Province Name (full name)
stateOrProvinceName_default     = Some-State

localityName                    = Locality Name (eg, city)

0.organizationName              = Organization Name (eg, company)
0.organizationName_default      = Internet Widgits Pty Ltd

organizationalUnitName          = Organizational Unit Name (eg, section)

commonName                      = Common Name (e.g. server FQDN or YOUR name)
commonName_max                  = 64

emailAddress                    = Email Address
emailAddress_max                = 64
EOF
) > "${OPENSSL_CONF}"

openssl genrsa -out "${TEST_CERT_DIR}/unittestkey.pem" 1024
openssl req -new -key "${TEST_CERT_DIR}/unittestkey.pem" -out "${TEST_CERT_DIR}/unittestcert.csr" \
    -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Unit Test CA
unittest@lyft.com


EOF
openssl x509 -req -days 15 -in "${TEST_CERT_DIR}/unittestcert.csr" -sha256 \
    -signkey "${TEST_CERT_DIR}/unittestkey.pem" -out "${TEST_CERT_DIR}/unittestcert.pem"
