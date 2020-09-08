#!/bin/bash -e

export NAME=front-proxy

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service: localhost:8080/service/1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    http://localhost:8080/service/1

run_log "Test service: localhost:8080/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    http://localhost:8080/service/2

run_log "Test service: https://localhost:8443/service/1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    -k https://localhost:8443/service/1

run_log "Scale up docker service1=3"
docker-compose scale service1=3
run_log "Snooze for 5 while docker-compose scales..."
sleep 5

run_log "Test round-robin localhost:8080/service/1"
docker-compose exec -T front-envoy bash -c "\
                   curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1" \
                   | grep Hello | grep "service 1"


run_log "Test service inside front-envoy: localhost:8080/service/2"
docker-compose exec -T front-envoy curl -s http://localhost:8080/service/2 | grep Hello | grep "service 2"

run_log "Test service info: localhost:8080/server_info"
docker-compose exec -T front-envoy curl -s http://localhost:8001/server_info | jq '.'

run_log "Test service stats: localhost:8080/stats"
docker-compose exec -T front-envoy curl -s http://localhost:8001/stats | grep ":"
