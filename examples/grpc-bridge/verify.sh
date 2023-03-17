#!/bin/bash -e

export NAME=grpc-bridge
# this allows us to bring up the stack manually after generating stubs
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Generate protocol stubs"
"$DOCKER_COMPOSE" -f docker-compose-protos.yaml up
docker rm grpc-bridge_stubs_go_1 grpc-bridge_stubs_python_1

ls client/kv/kv_pb2.py
ls server/kv/kv.pb.go

bring_up_example

run_log "Set key value foo=bar"
"$DOCKER_COMPOSE" exec -T grpc-client /client/grpc-kv-client.py set foo bar | grep setf

run_log "Get key foo"
"$DOCKER_COMPOSE" exec -T grpc-client /client/grpc-kv-client.py get foo | grep bar
