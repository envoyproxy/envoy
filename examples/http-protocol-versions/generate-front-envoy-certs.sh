#!/bin/bash -e

openssl req -x509 -newkey rsa:2048 \
    -keyout serverkey.pem \
    -out  servercert.pem \
    -days 3650 -nodes -subj '/CN=front-envoy'
