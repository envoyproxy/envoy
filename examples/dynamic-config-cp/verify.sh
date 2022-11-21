#!/bin/bash -e

export NAME=dynamic-config-cp
export UPARGS=" proxy"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Check port 10000 is not open (still shows as succeeded)"
nc -zv localhost 10000 |& grep -v open

run_log "Check the static cluster"
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].static_clusters' \
    | grep 'go-control-plane'

run_log "Check there is no config for dynamic clusters"
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters // "NO_CLUSTERS"' \
    | grep NO_CLUSTERS

run_log "Bring up go-control-plane"
"$DOCKER_COMPOSE" up --build -d go-control-plane
wait_for 30 sh -c "${DOCKER_COMPOSE} ps go-control-plane | grep healthy | grep -v unhealthy"
wait_for 10 bash -c "responds_with 'Request served by service1' http://localhost:10000"

run_log "Check for response from service1 backend"
responds_with \
    "Request served by service1" \
    http://localhost:10000

run_log "Check config for active clusters"
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"version_info": "1"'
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"address": "service1"'

run_log "Bring down the control plane"
"$DOCKER_COMPOSE" stop go-control-plane

wait_for 10 sh -c "\
         curl -s http://localhost:19000/config_dump \
         | jq -r '.configs[1].dynamic_active_clusters' \
         | grep '\"version_info\": \"1\"'"

run_log "Check for continued response from service1 backend"
responds_with \
    "Request served by service1" \
    http://localhost:10000

run_log "Check config for active clusters"
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"version_info": "1"'
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"address": "service1"'

run_log "Edit resource.go"
sed -i'.bak' s/service1/service2/ resource.go
sed -i'.bak' s/\"1\",/\"2\",/ resource.go

run_log "Bring back up the control plane"
"$DOCKER_COMPOSE" up --build -d go-control-plane
wait_for 30 sh -c "${DOCKER_COMPOSE} ps go-control-plane | grep healthy | grep -v unhealthy"

run_log "Check for response from service2 backend"
responds_with \
    "Request served by service2" \
    http://localhost:10000

run_log "Check config for active clusters pointing to service2"
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"version_info": "2"'
curl -s http://localhost:19000/config_dump \
    | jq -r '.configs[1].dynamic_active_clusters' \
    | grep '"address": "service2"'
