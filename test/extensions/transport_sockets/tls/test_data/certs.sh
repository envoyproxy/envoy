#!/bin/bash

set -e

readonly DEFAULT_VALIDITY_DAYS=${DEFAULT_VALIDITY_DAYS:-730}
readonly HERE=$(cd $(dirname $0) && pwd)

cd $HERE
trap cleanup EXIT

cleanup() {
  rm *csr
  rm *srl
  rm crl_*
}

# $1=<CA name> $2=[issuer name]
generate_ca() {
  if [[ "$2" != "" ]]; then local EXTRA_ARGS="-CA $2_cert.pem -CAkey $2_key.pem -CAcreateserial"; fi
  openssl genrsa -out $1_key.pem 2048
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1_cert.cfg -batch -sha256
  openssl x509 -req -days ${DEFAULT_VALIDITY_DAYS} -in $1_cert.csr -signkey $1_key.pem -out $1_cert.pem \
    -extensions v3_ca -extfile $1_cert.cfg $EXTRA_ARGS
}

# $1=<certificate name> $2=[key size] $3=[password]
generate_rsa_key() {
  if [[ "$2" != "" ]]; then local KEYSIZE=$2; else local KEYSIZE="2048"; fi
  if [[ "$3" != "" ]]; then echo -n "$3" > $1_password.txt; local EXTRA_ARGS="-aes128 -passout file:$1_password.txt"; fi
  openssl genrsa -out $1_key.pem $EXTRA_ARGS $KEYSIZE
}

# $1=<certificate name> $2=[curve]
generate_ecdsa_key() {
  if [[ "$2" != "" ]]; then local CURVE=$2; else local CURVE="secp256r1"; fi
  openssl ecparam -name $CURVE -genkey -out $1_key.pem
}

# $1=<certificate name>
generate_info_header() {
  echo "// NOLINT(namespace-envoy)" > $1_cert_info.h
  echo -e "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_HASH[] =\n    \"$(openssl x509 -in $1_cert.pem -outform DER | openssl dgst -sha256 | cut -d" " -f2)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_SPKI[] = \"$(openssl x509 -in $1_cert.pem -noout -pubkey | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl enc -base64)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_SERIAL[] = \"$(openssl x509 -in $1_cert.pem -noout -serial | cut -d"=" -f2 | awk '{print tolower($0)}')\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_NOT_BEFORE[] = \"$(openssl x509 -in $1_cert.pem -noout -startdate | cut -d"=" -f2)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_NOT_AFTER[] = \"$(openssl x509 -in $1_cert.pem -noout -enddate | cut -d"=" -f2)\";" >> $1_cert_info.h
}

# $1=<certificate name> $2=<CA name> $3=[days]
generate_x509_cert() {
  if [[ "$3" != "" ]]; then local DAYS=$3; else local DAYS="${DEFAULT_VALIDITY_DAYS}"; fi
  if [[ -f $1_password.txt ]]; then local EXTRA_ARGS="-passin file:$1_password.txt"; fi
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1_cert.cfg -batch -sha256 $EXTRA_ARGS
  openssl x509 -req -days $DAYS -in $1_cert.csr -sha256 -CA $2_cert.pem -CAkey \
    $2_key.pem -CAcreateserial -out $1_cert.pem -extensions v3_ca -extfile $1_cert.cfg $EXTRA_ARGS
  generate_info_header $1
}

# $1=<certificate name> $2=<CA name> $3=[days]
#
# Generate a certificate without a subject CN. For this to work, the config
# must have an empty [req_distinguished_name] section.
generate_x509_cert_nosubject() {
  if [[ "$3" != "" ]]; then local DAYS=$3; else local DAYS="${DEFAULT_VALIDITY_DAYS}"; fi
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1_cert.cfg -subj / -batch -sha256
  openssl x509 -req -days $DAYS -in $1_cert.csr -sha256 -CA $2_cert.pem -CAkey \
    $2_key.pem -CAcreateserial -out $1_cert.pem -extensions v3_ca -extfile $1_cert.cfg
  generate_info_header $1
}

# $1=<certificate name> $2=[certificate file name]
generate_selfsigned_x509_cert() {
  if [[ "$2" != "" ]]; then local OUTPUT_PREFIX=$2; else local OUTPUT_PREFIX=$1; fi
  openssl req -new -x509 -days ${DEFAULT_VALIDITY_DAYS} -key $1_key.pem -out ${OUTPUT_PREFIX}_cert.pem -config $1_cert.cfg -batch -sha256
  generate_info_header $OUTPUT_PREFIX
}

# Generate ca_cert.pem.
generate_ca ca
generate_x509_cert ca ca

# Generate intermediate_ca_cert.pem.
generate_ca intermediate_ca ca

# Generate fake_ca_cert.pem.
generate_ca fake_ca

# Concatenate Fake CA (fake_ca_cert.pem) and Test CA (ca_cert.pem) to create CA file with multiple entries.
cat fake_ca_cert.pem ca_cert.pem > ca_certificates.pem

# Generate no_san_cert.pem.
generate_rsa_key no_san
generate_x509_cert no_san ca

# Concatenate no_san_cert.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat no_san_cert.pem intermediate_ca_cert.pem > no_san_chain.pem

# Generate san_dns_cert.pem.
generate_rsa_key san_dns
generate_x509_cert san_dns ca

# Generate san_dns2_cert.pem (duplicate of san_dns_cert.pem, but with a different private key).
cp -f san_dns_cert.cfg san_dns2_cert.cfg
generate_rsa_key san_dns2
generate_x509_cert san_dns2 ca
rm -f san_dns2_cert.cfg

# Generate san_dns3_cert.pm (signed by intermediate_ca_cert.pem).
cp -f san_dns_cert.cfg san_dns3_cert.cfg
generate_rsa_key san_dns3
generate_x509_cert san_dns3 intermediate_ca
rm -f san_dns3_cert.cfg

# Concatenate san_dns3_cert.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat san_dns3_cert.pem intermediate_ca_cert.pem > san_dns3_chain.pem

# Generate san_multiple_dns_cert.pem.
generate_rsa_key san_multiple_dns
generate_x509_cert san_multiple_dns ca

# Generate san_only_dns_cert.pem.
generate_rsa_key san_only_dns
generate_x509_cert san_only_dns ca

# Generate san_uri_cert.pem.
generate_rsa_key san_uri
generate_x509_cert san_uri ca

# Generate password_protected_cert.pem.
cp -f san_uri_cert.cfg password_protected_cert.cfg
generate_rsa_key password_protected "" "p4ssw0rd"
generate_x509_cert password_protected ca
rm -f password_protected_cert.cfg

# Generate selfsigned*_cert.pem.
generate_rsa_key selfsigned
generate_selfsigned_x509_cert selfsigned
generate_selfsigned_x509_cert selfsigned selfsigned2

# Generate selfsigned_rsa_1024.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_1024_cert.cfg
generate_rsa_key selfsigned_rsa_1024 1024
generate_selfsigned_x509_cert selfsigned_rsa_1024
rm -f selfsigned_rsa_1024_cert.cfg

# Generate selfsigned_rsa_3072.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_3072_cert.cfg
generate_rsa_key selfsigned_rsa_3072 3072
generate_selfsigned_x509_cert selfsigned_rsa_3072
rm -f selfsigned_rsa_3072_cert.cfg

# Generate selfsigned_rsa_4096.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_4096_cert.cfg
generate_rsa_key selfsigned_rsa_4096 4096
generate_selfsigned_x509_cert selfsigned_rsa_4096
rm -f selfsigned_rsa_4096_cert.cfg

# Generate selfsigned_ecdsa_p256_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p256_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p256
generate_selfsigned_x509_cert selfsigned_ecdsa_p256
generate_selfsigned_x509_cert selfsigned_ecdsa_p256 selfsigned2_ecdsa_p256
rm -f selfsigned_ecdsa_p256_cert.cfg

# Generate selfsigned_ecdsa_p384_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p384_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p384 secp384r1
generate_selfsigned_x509_cert selfsigned_ecdsa_p384
rm -f selfsigned_ecdsa_p384_cert.cfg

# Generate long_validity_cert.pem as a self-signed, with expiry that exceeds 32bit time_t.
cp -f selfsigned_cert.cfg long_validity_cert.cfg
generate_rsa_key long_validity
generate_x509_cert long_validity ca 18250
rm -f long_validity_cert.cfg

# Generate expired_cert.pem as a self-signed, expired cert (will fail on macOS 10.13+ because of negative days value).
cp -f selfsigned_cert.cfg expired_cert.cfg
generate_rsa_key expired
generate_x509_cert expired ca -365
rm -f expired_cert.cfg

# Generate expired_san_uri_cert.pem as a CA signed, expired cert (will fail on macOS 10.13+ because of negative days value).
cp -f san_uri_cert.cfg expired_san_uri_cert.cfg
generate_rsa_key expired_san_uri
generate_x509_cert expired_san_uri ca -365
rm -f expired_san_uri_cert.cfg

# Initialize information for CRL process
touch crl_index.txt crl_index.txt.attr
echo 00 > crl_number

# Revoke the certificate and generate a CRL
openssl ca -revoke san_dns_cert.pem -keyfile ca_key.pem -cert ca_cert.pem -config ca_cert.cfg
openssl ca -gencrl -keyfile ca_key.pem -cert ca_cert.pem -out ca_cert.crl -config ca_cert.cfg
cat ca_cert.pem ca_cert.crl > ca_cert_with_crl.pem

# Write session ticket key files
openssl rand 80 > ticket_key_a
openssl rand 80 > ticket_key_b
openssl rand 79 > ticket_key_wrong_len

# Generate a certificate with no subject CN and no altnames.
generate_rsa_key no_subject
generate_x509_cert_nosubject no_subject ca
