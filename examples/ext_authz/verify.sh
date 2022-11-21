#!/bin/bash -e

export NAME=ext_authz
export PORT_PROXY="${EXT_AUTH_PORT_PROXY:-10500}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services responds with 403"
responds_with_header \
    "HTTP/1.1 403 Forbidden"\
    "http://localhost:${PORT_PROXY}/service"

run_log "Restart front-envoy with FRONT_ENVOY_YAML=config/http-service.yaml"
"$DOCKER_COMPOSE" down
FRONT_ENVOY_YAML=config/http-service.yaml "$DOCKER_COMPOSE" up -d

wait_for 15 bash -c "\
         responds_with_header \
         'HTTP/1.1 200 OK' \
         -H 'Authorization: Bearer token1' \
         http://localhost:${PORT_PROXY}/service"

run_log "Test service responds with 403"
responds_with_header \
    "HTTP/1.1 403 Forbidden"\
    "http://localhost:${PORT_PROXY}/service"

run_log "Test authenticated service responds with 200"
responds_with_header \
    "HTTP/1.1 200 OK" \
    -H "Authorization: Bearer token1" \
    "http://localhost:${PORT_PROXY}/service"

run_log "Restart front-envoy with FRONT_ENVOY_YAML=config/opa-service/v3.yaml"
"$DOCKER_COMPOSE" down
FRONT_ENVOY_YAML=config/opa-service/v3.yaml "$DOCKER_COMPOSE" up -d
wait_for 15 bash -c "\
         responds_with_header \
         'HTTP/1.1 200 OK' \
         http://localhost:${PORT_PROXY}/service"

run_log "Test OPA service responds with 200"
responds_with_header \
    "HTTP/1.1 200 OK" \
    "http://localhost:${PORT_PROXY}/service"

run_log "Check OPA logs"
"$DOCKER_COMPOSE" logs ext_authz-opa-service | grep decision_id -A 30

run_log "Check OPA service rejects POST"
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    -X POST \
    "http://localhost:${PORT_PROXY}/service"
