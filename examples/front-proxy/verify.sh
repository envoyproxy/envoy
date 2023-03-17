#!/bin/bash -e

export NAME=front-proxy
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-10600}"
export PORT_HTTPS="${FRONT_PROXY_PORT_HTTPS:-10601}"
export PORT_STATS="${FRONT_PROXY_PORT_STATS:-10602}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service: localhost:${PORT_PROXY}/service/1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/service/1"

run_log "Test service: localhost:${PORT_PROXY}/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2"

run_log "Test service: https://localhost:${PORT_HTTPS}/service/1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    -k "https://localhost:${PORT_HTTPS}/service/1"

run_log "Scale up docker service1=3"
"$DOCKER_COMPOSE" up -d --scale service1=3
run_log "Snooze for 5 while $DOCKER_COMPOSE scales..."
sleep 5

run_log "Test round-robin localhost:${PORT_PROXY}/service/1"
"$DOCKER_COMPOSE" exec -T front-envoy bash -c "\
                   curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1" \
                   | grep Hello | grep "service 1"

run_log "Test service inside front-envoy: localhost:${PORT_PROXY}/service/2"
"$DOCKER_COMPOSE" exec -T front-envoy curl -s "http://localhost:8080/service/2" | grep Hello | grep "service 2"

run_log "Test service info: localhost:${PORT_STATS}/server_info"
"$DOCKER_COMPOSE" exec -T front-envoy curl -s "http://localhost:8001/server_info" | jq '.'

run_log "Test service stats: localhost:${PORT_STATS}/stats"
"$DOCKER_COMPOSE" exec -T front-envoy curl -s "http://localhost:8001/stats" | grep ":"
