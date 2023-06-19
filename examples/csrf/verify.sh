#!/bin/bash -e

export NAME=csrf
export PATHS=samesite,crosssite

export PORT_SAME="${CSRF_PORT_SAME:-10320}"
export PORT_STATS="${CSRF_PORT_STATS:-10321}"
export PORT_CROSS="${CSRF_PORT_CROSS:-10322}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services"
responds_with \
    "Envoy CSRF Demo" \
    "http://localhost:${PORT_CROSS}"
responds_with \
    "Envoy CSRF Demo" \
    "http://localhost:${PORT_SAME}"

run_log "Test stats server"
responds_with \
    ":" \
    "http://localhost:${PORT_STATS}/stats"

run_log "Test csrf server: disabled"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/disabled"
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/disabled"

run_log "Test csrf server: shadow"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/shadow"
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/shadow"

run_log "Test csrf server: enabled"
responds_with \
    "Invalid origin" \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/enabled"
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/enabled"

run_log "Test csrf server: additional_origin"
responds_with \
    Success \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/additional_origin"
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -X POST \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_SAME}/csrf/additional_origin"

run_log "Check admin ingress stats"
responds_with \
    ingress_http.csrf \
    "http://localhost:${PORT_STATS}/stats?filter=ingress_http"
