#!/bin/bash -e

export NAME=cors
export PATHS=frontend,backend

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service"
responds_with \
    "Envoy CORS Webpage" \
    http://localhost:8000

run_log "Test cors server: disabled"
responds_with \
    Success \
    -H "Origin: http://example.com" \
    http://localhost:8002/cors/disabled
responds_without_header \
    access-control-allow-origin \
    -H "Origin: http://example.com" \
    http://localhost:8002/cors/disabled

run_log "Test cors server: open"
responds_with \
    Success \
    -H 'Origin: http://example.com' \
    http://localhost:8002/cors/open
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -H "Origin: http://example.com" \
    http://localhost:8002/cors/open

run_log "Test cors server: restricted"
responds_with \
    Success \
    -H "Origin: http://example.com" \
    http://localhost:8002/cors/restricted
responds_without_header \
    access-control-allow-origin \
    -H "Origin: http://example.com" \
    http://localhost:8002/cors/restricted
responds_with_header \
    "access-control-allow-origin: http://foo.envoyproxy.io" \
    -H "Origin: http://foo.envoyproxy.io" \
    http://localhost:8002/cors/restricted

run_log "Check admin ingress stats"
responds_with \
    ingress_http.cors \
    "http://localhost:8003/stats?filter=ingress_http"
