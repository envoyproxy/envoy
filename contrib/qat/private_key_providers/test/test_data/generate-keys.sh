#!/bin/bash

set -e

openssl ecparam -name prime256v1 -genkey -noout -out ecdsa-p256.pem
openssl genrsa -out rsa-1024.pem 1024
openssl genrsa -out rsa-2048.pem 2048
openssl genrsa -out rsa-3072.pem 3072
openssl genrsa -out rsa-4096.pem 4096

