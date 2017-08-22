#!/bin/bash

set -e

# Uncomment the following lines if you want to regenerate the private keys.
# openssl genrsa -out cakey.pem 1024
# openssl genrsa -out serverkey.pem 1024
# openssl genrsa -out clientkey.pem 1024
# openssl genrsa -out upstreamcakey.pem 1024
# openssl genrsa -out upstreamkey.pem 1024

# Generate cert for the CA.
openssl req -new -key cakey.pem -out cacert.csr -config cacert.cfg -batch -sha256
openssl x509 -req -days 730 -in cacert.csr -signkey cakey.pem -out cacert.pem -extensions v3_ca -extfile cacert.cfg

# Generate cert for the server.
openssl req -new -key serverkey.pem -out servercert.csr -config servercert.cfg -batch -sha256
openssl x509 -req -days 730 -in servercert.csr -sha256 -CA cacert.pem -CAkey cakey.pem -CAcreateserial -out servercert.pem -extensions v3_ca -extfile servercert.cfg

# Generate cert for the client.
openssl req -new -key clientkey.pem -out clientcert.csr -config clientcert.cfg -batch -sha256
openssl x509 -req -days 730 -in clientcert.csr -sha256 -CA cacert.pem -CAkey cakey.pem -CAcreateserial -out clientcert.pem -extensions v3_ca -extfile clientcert.cfg

# Generate cert for the upstream CA.
openssl req -new -key upstreamcakey.pem -out upstreamcacert.csr -config upstreamcacert.cfg -batch -sha256
openssl x509 -req -days 730 -in upstreamcacert.csr -signkey upstreamcakey.pem -out upstreamcacert.pem -extensions v3_ca -extfile upstreamcacert.cfg

# Generate cert for the upstream node.
openssl req -new -key upstreamkey.pem -out upstreamcert.csr -config upstreamcert.cfg -batch -sha256
openssl x509 -req -days 730 -in upstreamcert.csr -sha256 -CA upstreamcacert.pem -CAkey upstreamcakey.pem -CAcreateserial -out upstreamcert.pem -extensions v3_ca -extfile upstreamcert.cfg

rm *.csr
rm *.srl
