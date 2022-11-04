#!/bin/bash -e

export NAME=route-mirroring
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-11820}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Make a request to the statically mirrored route"
responds_with "Hello from behind Envoy (service 1)!" "http://localhost:${PORT_PROXY}/service/1"

run_log "View logs for the request mirrored by request header"
docker-compose logs service1 | grep --quiet "Host: localhost:${PORT_PROXY}"
docker-compose logs service1-mirror | grep --quiet "Host: localhost-shadow:${PORT_PROXY}"
docker-compose logs service1-mirror | grep --quiet GET

run_log "Make a request to the route mirrored by request header"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2" \
    --header 'x-mirror-cluster: service2-mirror'

run_log "View logs for the request mirrored by request header"
docker-compose logs service2 | grep --quiet "Host: localhost:${PORT_PROXY}"
docker-compose logs service2-mirror | grep --quiet "Host: localhost-shadow:${PORT_PROXY}"
docker-compose logs service2-mirror | grep --quiet GET

run_log "Missing or invalid cluster name in request header"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2" \
    --header 'x-mirror-cluster: service2-mirror-non-existent'
docker-compose logs service2-mirror | grep -c GET | grep --quiet 1
