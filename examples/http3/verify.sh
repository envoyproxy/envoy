#!/bin/bash -e

export NAME=http3
export PORT_PROXY_HTTP3="${HTTP3_PORT_PROXY:-10800}"
export PORT_PROXY_HTTP="${PORT_PROXY_HTTP:-10801}"
export PORT_ADMIN="${HTTP3_PORT_ADMIN:-10802}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test HTTP/3 donwstream to HTTP/1.1 upstream"
responds_with_header \
    "HTTP/3 301" \
    "https://localhost:${PORT_PROXY_HTTP3}/downstream-http3-to-upstream-http1" \
    --http3 -ki

run_log "Test HTTP/3 donwstream to HTTP/2 upstream"
responds_with_header \
    "HTTP/3 301" \
    "https://localhost:${PORT_PROXY_HTTP3}/downstream-http3-to-upstream-http2" \
    --http3 -ki

run_log "Test HTTP/2 downstream to HTTP/3 upstream"
responds_with_header \
    "HTTP/2 404" \
    "http://localhost:${PORT_PROXY_HTTP}/downstream-http2-to-upstream-http3" \
    --http2-prior-knowledge -ki

run_log "Test HTTP/1 downstream to HTTP/3 upstream"
responds_with_header \
    "HTTP/1.1 404" \
    "http://localhost:${PORT_PROXY_HTTP}/downstream-http1-to-upstream-http3" \
    -ki

run_log "Check total ingress HTTP/3 connections stats"
responds_with \
    "http.ingress_http.downstream_cx_http3_total: 2" \
    http://localhost:${PORT_ADMIN}/stats?filter=downstream_cx_http3_total

run_log "Check total ingress HTTP/2 connections stats"
responds_with \
    "http.ingress_http.downstream_cx_http2_total: 1" \
    http://localhost:${PORT_ADMIN}/stats?filter=downstream_cx_http2_total

run_log "Check total ingress HTTP/1.1 connections stats"
responds_with \
    "http.ingress_http.downstream_cx_http1_total: 1" \
    http://localhost:${PORT_ADMIN}/stats?filter=downstream_cx_http1_total
