#!/bin/bash

set -e

# Uncomment the following lines if you want to regenerate the private keys.
# openssl genrsa -out ca_key.pem 1024
# openssl genrsa -out fake_ca_key.pem 1024
# openssl genrsa -out no_san_key.pem 1024
# openssl genrsa -out san_dns_key.pem 1024
# openssl genrsa -out san_dns_key2.pem 1024
# openssl genrsa -out san_multiple_dns_key.pem 1024
# openssl genrsa -out san_uri_key.pem 1024
# openssl genrsa -out selfsigned_key.pem 1024
# openssl genrsa -out expired_key.pem 1024

# Generate ca_cert.pem.
openssl req -new -key ca_key.pem -out ca_cert.csr -config ca_cert.cfg -batch -sha256
openssl x509 -req -days 3650 -in ca_cert.csr -signkey ca_key.pem -out ca_cert.pem -extensions v3_ca -extfile ca_cert.cfg

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

# Generate san_dns_cert2.pem.
openssl req -new -key san_dns_key2.pem -out san_dns_cert2.csr -config san_dns_cert.cfg -batch -sha256
openssl x509 -req -days 730 -in san_dns_cert2.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_dns_cert2.pem -extensions v3_ca -extfile san_dns_cert.cfg

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

# Generate expired_cert.pem (will fail on Mac OS 10.13+ because of negative days value).
openssl req -new -key expired_key.pem -out expired_cert.csr -config selfsigned_cert.cfg -batch -sha256
openssl x509 -req -days -365 -in expired_cert.csr -signkey expired_key.pem -out expired_cert.pem

# Write session ticket key files
openssl rand 80 > ticket_key_a
openssl rand 80 > ticket_key_b
openssl rand 79 > ticket_key_wrong_len

rm *csr
rm *srl
