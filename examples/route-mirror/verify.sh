#!/bin/bash -e

export NAME=route-mirroring
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-11820}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Static mirror cluster: localhost:${PORT_PROXY}/service/1"
wait_for 20 bash -c "responds_with \"Hello from behind Envoy (service 1)!\" http://localhost:${PORT_PROXY}/service/1"

docker-compose logs service1 | grep --quiet "Host: localhost:${PORT_PROXY}"
docker-compose logs service1-mirror | grep --quiet "Host: localhost-shadow:${PORT_PROXY}"


run_log "Mirror cluster via header: x-mirror-cluster localhost:${PORT_PROXY}/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2" \
    --header 'x-mirror-cluster: service2-mirror'

docker-compose logs service2 | grep --quiet "Host: localhost:${PORT_PROXY}"
docker-compose logs service2-mirror | grep --quiet "Host: localhost-shadow:${PORT_PROXY}"
