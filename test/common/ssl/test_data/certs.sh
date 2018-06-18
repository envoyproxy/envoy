#!/bin/bash

set -e

# Uncomment the following lines if you want to regenerate the private keys.
# openssl genrsa -out ca_key.pem 1024
# openssl genrsa -out intermediate_ca_key.pem 1024
# openssl genrsa -out fake_ca_key.pem 1024
# openssl genrsa -out no_san_key.pem 1024
# openssl genrsa -out san_dns_key.pem 1024
# openssl genrsa -out san_dns_key2.pem 1024
# openssl genrsa -out san_dns_key3.pem 1024
# openssl genrsa -out san_multiple_dns_key.pem 1024
# openssl genrsa -out san_uri_key.pem 1024
# openssl genrsa -out selfsigned_key.pem 1024
# openssl genrsa -out expired_key.pem 1024
# openssl genrsa -out expired_san_uri_key.pem 1024

# Generate ca_cert.pem.
openssl req -new -key ca_key.pem -out ca_cert.csr -config ca_cert.cfg -batch -sha256
openssl x509 -req -days 3650 -in ca_cert.csr -signkey ca_key.pem -out ca_cert.pem -extensions v3_ca -extfile ca_cert.cfg

# Generate intermediate_ca_cert.pem.
openssl req -new -key intermediate_ca_key.pem -out intermediate_ca_cert.csr -config intermediate_ca_cert.cfg -batch -sha256
openssl x509 -req -days 3650 -in intermediate_ca_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out intermediate_ca_cert.pem -extensions v3_ca -extfile intermediate_ca_cert.cfg

# Generate fake_ca_cert.pem.
openssl req -new -key fake_ca_key.pem -out fake_ca_cert.csr -config fake_ca_cert.cfg -batch -sha256
openssl x509 -req -days 3650 -in fake_ca_cert.csr -signkey fake_ca_key.pem -out fake_ca_cert.pem -extensions v3_ca -extfile fake_ca_cert.cfg

# Concatenate Fake CA (fake_ca_cert.pem) and Test CA (ca_cert.pem) to create CA file with multiple entries.
cat fake_ca_cert.pem ca_cert.pem > ca_certificates.pem

# Generate no_san_cert.pem.
openssl req -new -key no_san_key.pem -out no_san_cert.csr -config no_san_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in no_san_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out no_san_cert.pem -extensions v3_ca -extfile no_san_cert.cfg

# Generate san_dns_cert.pem.
openssl req -new -key san_dns_key.pem -out san_dns_cert.csr -config san_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_dns_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_dns_cert.pem -extensions v3_ca -extfile san_dns_cert.cfg

# Generate san_dns_cert2.pem (duplicate of san_dns_cert.pem, but with a different private key).
openssl req -new -key san_dns_key2.pem -out san_dns_cert2.csr -config san_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_dns_cert2.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_dns_cert2.pem -extensions v3_ca -extfile san_dns_cert.cfg

# Generate san_dns_cert3.pem (signed by intermediate_ca_cert.pem).
openssl req -new -key san_dns_key3.pem -out san_dns_cert3.csr -config san_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_dns_cert3.csr -sha256 -CA intermediate_ca_cert.pem -CAkey intermediate_ca_key.pem -CAcreateserial -out san_dns_cert3.pem -extensions v3_ca -extfile san_dns_cert.cfg

# Concatenate san_dns_cert3.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat san_dns_cert3.pem intermediate_ca_cert.pem > san_dns_chain3.pem

# Generate san_multiple_dns_cert.pem.
openssl req -new -key san_multiple_dns_key.pem -out san_multiple_dns_cert.csr -config san_multiple_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_multiple_dns_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_multiple_dns_cert.pem -extensions v3_ca -extfile san_multiple_dns_cert.cfg

# Generate san_only_dns_cert.pem.
openssl req -new -key san_only_dns_key.pem -out san_only_dns_cert.csr -config san_only_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_only_dns_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_only_dns_cert.pem -extensions v3_ca -extfile san_only_dns_cert.cfg

# Generate san_uri_cert.pem.
openssl req -new -key san_uri_key.pem -out san_uri_cert.csr -config san_uri_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_uri_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_uri_cert.pem -extensions v3_ca -extfile san_uri_cert.cfg

# Generate selfsigned_cert.pem.
openssl req -new -x509 -days 730 -key selfsigned_key.pem -out selfsigned_cert.pem -config selfsigned_cert.cfg -batch -sha256

# Generate expired_cert.pem as a self-signed, expired cert (will fail on Mac OS 10.13+ because of negative days value).
openssl req -new -key expired_key.pem -out expired_cert.csr -config selfsigned_cert.cfg -batch -sha256
openssl x509 -req -days -365 -in expired_cert.csr -signkey expired_key.pem -out expired_cert.pem

# Generate expired_san_uri_cert.pem as a CA signed, expired cert (will fail on Mac OS 10.13+ because of negative days value).
openssl req -new -key expired_san_uri_key.pem -out expired_san_uri_cert.csr -config san_uri_cert.cfg -batch -sha256
openssl x509 -req -days -365 -in expired_san_uri_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out expired_san_uri_cert.pem -extensions v3_ca -extfile san_uri_cert.cfg

# Initialize information for CRL process
touch crl_index.txt crl_index.txt.attr
echo 00 > crl_number

# Revoke the certificate and generate a CRL
openssl ca -revoke san_dns_cert.pem -keyfile ca_key.pem -cert ca_cert.pem -config ca_cert.cfg
openssl ca -gencrl -keyfile ca_key.pem -cert ca_cert.pem -out ca_cert.crl -config ca_cert.cfg

# Write session ticket key files
openssl rand 80 > ticket_key_a
openssl rand 80 > ticket_key_b
openssl rand 79 > ticket_key_wrong_len

rm *csr
rm *srl
rm crl_*
