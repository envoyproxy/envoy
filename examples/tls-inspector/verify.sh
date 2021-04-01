#!/bin/bash -e

export NAME=tls-inspector

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Curl tls inspector: HTTPS -> HTTP/1.1"
curl -sk --http1.1 https://localhost:10000  | jq  '.os.hostname' | grep service-https-http1.1

run_log "Curl tls inspector: HTTPS -> HTTP/2"
curl -sk --http2 https://localhost:10000  | jq  '.os.hostname' | grep service-https-http2

run_log "Curl tls inspector: HTTP"
curl -s http://localhost:10000  | jq  '.os.hostname' | grep service-http

run_log "Check stats of tls inspector"
curl -s http://localhost:12345/stats | grep "tls_inspector.alpn_found: 2"
curl -s http://localhost:12345/stats | grep "tls_inspector.sni_found: 2"
curl -s http://localhost:12345/stats | grep "tls_inspector.tls_found: 2"
curl -s http://localhost:12345/stats | grep "tls_inspector.tls_not_found: 1"
