#!/bin/bash -e

export NAME=http-protocol-options
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

set -e

run_log "Generate front envoy certificates"
openssl req -x509 -newkey rsa:2048 \
    -keyout serverkey.pem \
    -out  servercert.pem \
    -days 3650 -nodes -subj '/CN=front-envoy'

run_log "Bring up HTTP protocol stack"
bring_up_example

path_list=( http11 http2 http3 )

run_log "Test HTTP1.1 -> HTTP1.1/HTTP2/HTTP3"
for path in "${path_list[@]}"; do
    run_log "Check HTTP1.1 -> $path"
    responds_with_header "HTTP/1.1 200" \
        "https://localhost:10000/${path}" \
        -k --http1.1
done

run_log "Test HTTP2 -> HTTP1.1/HTTP2/HTTP3"
for path in "${path_list[@]}"; do
    run_log "Check HTTP2 -> $path"
    responds_with_header "HTTP/2 200" \
        "https://localhost:10001/${path}" \
        -k --http2 --http2-prior-knowledge
done

run_log "Test HTTP3 -> HTTP1.1/HTTP2/HTTP3"
for path in "${path_list[@]}"; do
    run_log "Check HTTP3 -> $path"
    # SNI parameter needs to be set explicitly because when hostname is localhost
    # crypto handshake fails because it could not figure out the value for SNI parameter
    docker-compose -f docker-compose-h3-client.yaml run \
        h3client /root/h3client --sni "test.proxy" --address "https://localhost:10002/${path}" 2>&1 | grep "SUCCESS"
done

docker-compose -f docker-compose-h3-client.yaml rm --force --stop

run_log "Clean up generated certificates and keys"
rm serverkey.pem || true
rm servercert.pem || true
