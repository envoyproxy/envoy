#!/bin/bash -e

export NAME=double-proxy
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

mkdir -p certs

# TODO(phlax): remove openssl bug workaround when openssl/ubuntu are updated
#    see #15555 for more info
touch ~/.rnd

run_log "Create a cert authority"
openssl genrsa -out certs/ca.key 4096
openssl req -batch -x509 -new -nodes -key certs/ca.key -sha256 -days 1024 -out certs/ca.crt

run_log "Create a domain key"
openssl genrsa -out certs/example.com.key 2048

run_log "Generate signing requests for each proxy"
openssl req -new -sha256 \
        -key certs/example.com.key \
        -subj "/C=US/ST=CA/O=MyExample, Inc./CN=proxy-postgres-frontend.example.com" \
        -out certs/proxy-postgres-frontend.example.com.csr
openssl req -new -sha256 \
        -key certs/example.com.key \
        -subj "/C=US/ST=CA/O=MyExample, Inc./CN=proxy-postgres-backend.example.com" \
        -out certs/proxy-postgres-backend.example.com.csr

run_log "Generate certificates for each proxy"
openssl x509 -req \
        -in certs/proxy-postgres-frontend.example.com.csr \
        -CA certs/ca.crt \
        -CAkey certs/ca.key \
        -CAcreateserial \
        -extfile <(printf "subjectAltName=DNS:proxy-postgres-frontend.example.com") \
        -out certs/postgres-frontend.example.com.crt \
        -days 500 \
        -sha256
openssl x509 -req \
        -in certs/proxy-postgres-backend.example.com.csr \
        -CA certs/ca.crt \
        -CAkey certs/ca.key \
        -CAcreateserial \
        -extfile <(printf "subjectAltName=DNS:proxy-postgres-backend.example.com") \
        -out certs/postgres-backend.example.com.crt \
        -days 500 \
        -sha256

bring_up_example

run_log "Test app/db connection"
responds_with \
    "Connected to Postgres, version: PostgreSQL" \
    http://localhost:10000
