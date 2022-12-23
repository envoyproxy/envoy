#!/bin/bash -e

export NAME=grpc-s2s
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-12000}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# TODO network=host
run_log "Make an example request"
docker run --network=host fullstorydev/grpcurl -plaintext "localhost:${PORT_PROXY}" Hello/Greet

run_log "Render an instance of Hello unhealthy"
docker-compose exec --index 1 hello kill -SIGUSR1 1

sleep 60
curl "http://localhost:9090/clusters" | grep failed_active_hc

docker run --network=host fullstorydev/grpcurl -plaintext "localhost:${PORT_PROXY}" Hello/Greet

run_log "Render an instance of World unhealthy"
docker-compose exec --index 1 world kill -SIGUSR1 1
sleep 60
curl "http://localhost:9091/clusters" | grep failed_active_hc

docker run --network=host fullstorydev/grpcurl -plaintext "localhost:${PORT_PROXY}" Hello/Greet


run_log "Query healthcheck metrics"
curl "http://localhost:9090/stats" | grep cluster.hello.health_check
curl "http://localhost:9091/stats" | grep cluster.world.health_check

