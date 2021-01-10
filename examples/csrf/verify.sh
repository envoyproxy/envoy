#!/bin/bash -e

export NAME=csrf
export PATHS=samesite,crosssite

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services"
responds_with \
    "Envoy CSRF Demo" \
    http://localhost:8002
responds_with \
    "Envoy CSRF Demo" \
    http://localhost:8000

run_log "Test stats server"
responds_with \
    ":" \
    http://localhost:8001/stats

run_log "Test csrf server: disabled"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/disabled
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/disabled

run_log "Test csrf server: shadow"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/shadow
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/shadow

run_log "Test csrf server: enabled"
responds_with \
    "Invalid origin" \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/enabled
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/enabled

run_log "Test csrf server: additional_origin"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/additional_origin
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    http://localhost:8000/csrf/additional_origin

run_log "Check admin ingress stats"
responds_with \
    ingress_http.csrf \
    "http://localhost:8001/stats?filter=ingress_http"
