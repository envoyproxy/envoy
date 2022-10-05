#!/bin/bash -e

export NAME=route-mirroring
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-10600}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service: localhost:${PORT_PROXY}/service/1"
wait_for 20 bash -c "responds_with \"Hello from behind Envoy (service 1)!\" http://localhost:${PORT_PROXY}/service/1"
docker logs --tail 1 route-mirroring-service1-mirror-1 2>&1 | grep --quiet "\"GET /service/1 HTTP/1.1\" 200 -"


run_log "Test service: localhost:${PORT_PROXY}/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2" \
    --header 'x-mirror-cluster: service2-mirror'
docker logs --tail 1 route-mirroring-service2-mirror-1 2>&1 | grep --quiet "\"GET /service/2 HTTP/1.1\" 200 -"
