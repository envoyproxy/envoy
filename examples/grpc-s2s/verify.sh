#!/bin/bash -e

export NAME=grpc-s2s
export PORT_PROXY="${GRPC_S2S_PORT_PROXY:-12000}"
export PORT_ADMIN_HELLO="${GRPC_S2S_HELLO_PORT_ADMIN:-12800}"
export PORT_ADMIN_WORLD="${GRPC_S2S_WORLD_PORT_ADMIN:-12801}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# TODO network=host
run_log "Make an example request to Hello which will call World as well"
docker run --network=host fullstorydev/grpcurl -plaintext "localhost:${PORT_PROXY}" Hello/Greet

run_log "Query healthy instances for envoy: Hello"
curl -s "http://localhost:${PORT_ADMIN_HELLO}/stats" | grep -q "cluster.hello.health_check.healthy: 2"

run_log "Query healthy instances for envoy: World"
curl -s "http://localhost:${PORT_ADMIN_WORLD}/stats" | grep -q "cluster.world.health_check.healthy: 2"

run_log "Render an instance of Hello unhealthy"
docker-compose exec -ti --index 1 hello kill -SIGUSR1 1
docker-compose logs hello | grep hello-1 |  grep -q "Marking service Hello as unhealthy"

run_log "Render an instance of World unhealthy"
docker-compose exec -ti --index 1 world kill -SIGUSR1 1
docker-compose logs world | grep world-1 | grep -q "Marking service World as unhealthy"

sleep 1

run_log "Ensure that we now only have one healthy instance of Hello"
curl -s "http://localhost:${PORT_ADMIN_HELLO}/stats" | grep -q "cluster.hello.health_check.healthy: 1"

run_log "Ensure that we now only have one healthy instance of World"
curl -s "http://localhost:${PORT_ADMIN_WORLD}/stats" | grep -q "cluster.world.health_check.healthy: 1"

run_log "Make a request to Hello which will call World as well"
docker run --network=host fullstorydev/grpcurl -plaintext "localhost:${PORT_PROXY}" Hello/Greet
