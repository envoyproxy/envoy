#!/bin/bash

set -e

# $1=<CA name> $2=[issuer name]
generate_ca() {
  if [[ "$2" != "" ]]; then local EXTRA_ARGS="-CA $2_cert.pem -CAkey $2_key.pem -CAcreateserial"; fi
  openssl genrsa -out $1_key.pem 1024
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1_cert.cfg -batch -sha256
  openssl x509 -req -days 730 -in $1_cert.csr -signkey $1_key.pem -out $1_cert.pem \
    -extensions v3_ca -extfile $1_cert.cfg $EXTRA_ARGS
}

# $1=<certificate name> $2=[password]
generate_rsa_key() {
  if [[ "$2" != "" ]]; then echo -n "$2" > $1_password.txt; local EXTRA_ARGS="-aes128 -passout file:$1_password.txt"; fi
  openssl genrsa -out $1_key.pem $EXTRA_ARGS 1024
}

# $1=<certificate name> $2=[curve]
generate_ecdsa_key() {
  if [[ "$2" != "" ]]; then local CURVE=$2; else local CURVE="secp256r1"; fi
  openssl ecparam -name $CURVE -genkey -out $1_key.pem
}

# $1=<certificate name> $2=<CA name> $3=[days]
generate_x509_cert() {
  if [[ "$3" != "" ]]; then local DAYS=$3; else local DAYS="730"; fi
  if [[ -f $1_password.txt ]]; then local EXTRA_ARGS="-passin file:$1_password.txt"; fi
  openssl req -new -key $1_key.pem -out $1_cert.csr -config $1_cert.cfg -batch -sha256 $EXTRA_ARGS
  openssl x509 -req -days $DAYS -in $1_cert.csr -sha256 -CA $2_cert.pem -CAkey \
    $2_key.pem -CAcreateserial -out $1_cert.pem -extensions v3_ca -extfile $1_cert.cfg $EXTRA_ARGS
  echo "// NOLINT(namespace-envoy)" > $1_cert_info.h
  echo -e "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_HASH[] =\n    \"$(openssl x509 -in $1_cert.pem -outform DER | openssl dgst -sha256 | cut -d" " -f2)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_SPKI[] = \"$(openssl x509 -in $1_cert.pem -noout -pubkey | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl enc -base64)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_SERIAL[] = \"$(openssl x509 -in $1_cert.pem -noout -serial | cut -d"=" -f2 | awk '{print tolower($0)}')\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_NOT_BEFORE[] = \"$(openssl x509 -in $1_cert.pem -noout -startdate | cut -d"=" -f2)\";" >> $1_cert_info.h
  echo "constexpr char TEST_$(echo $1 | tr a-z A-Z)_CERT_NOT_AFTER[] = \"$(openssl x509 -in $1_cert.pem -noout -enddate | cut -d"=" -f2)\";" >> $1_cert_info.h
}

# $1=<certificate name>
generate_selfsigned_x509_cert() {
  openssl req -new -x509 -days 730 -key $1_key.pem -out $1_cert.pem -config $1_cert.cfg -batch -sha256
}

# Generate ca_cert.pem.
generate_ca ca

# Generate intermediate_ca_cert.pem.
generate_ca intermediate_ca ca

# Generate fake_ca_cert.pem.
generate_ca fake_ca

# Concatenate Fake CA (fake_ca_cert.pem) and Test CA (ca_cert.pem) to create CA file with multiple entries.
cat fake_ca_cert.pem ca_cert.pem > ca_certificates.pem

# Generate no_san_cert.pem.
generate_rsa_key no_san
generate_x509_cert no_san ca

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
generate_rsa_key password_protected "p4ssw0rd"
generate_x509_cert password_protected ca
rm -f password_protected_cert.cfg

# Generate selfsigned_cert.pem.
generate_rsa_key selfsigned
generate_selfsigned_x509_cert selfsigned

# Generate selfsigned_ecdsa_p256_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p256_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p256
generate_selfsigned_x509_cert selfsigned_ecdsa_p256
rm -f selfsigned_ecdsa_p256_cert.cfg

# Generate selfsigned_ecdsa_p384_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p384_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p384 secp384r1
generate_selfsigned_x509_cert selfsigned_ecdsa_p384
rm -f selfsigned_ecdsa_p384_cert.cfg

# Generate expired_cert.pem as a self-signed, expired cert (will fail on Mac OS 10.13+ because of negative days value).
cp -f selfsigned_cert.cfg expired_cert.cfg
generate_rsa_key expired
generate_x509_cert expired ca -365
rm -f expired_cert.cfg

# Generate expired_san_uri_cert.pem as a CA signed, expired cert (will fail on Mac OS 10.13+ because of negative days value).
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

rm *csr
rm *srl
rm crl_*
