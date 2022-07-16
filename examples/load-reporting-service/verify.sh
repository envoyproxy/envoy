#!/bin/bash -e

export NAME=load-reporting
export UPARGS="--scale http_service=2"
export DELAY=10
export PORT_PROXY0="${LRS_PORT_PROXY0:-11200}"
export PORT_PROXY1="${LRS_PORT_PROXY1:-11201}"
export PORT_ADMIN="${LRS_PORT_ADMIN:-11202}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Send requests"
bash send_requests.sh 2> /dev/null
run_log "Check logs: http 1"
"${DOCKER_COMPOSE}" logs http_service | grep http_service_1 | grep HTTP | grep 200

run_log "Check logs: http 2"
"${DOCKER_COMPOSE}" logs http_service | grep http_service_2 | grep HTTP | grep 200

run_log "Check logs: lrs_server"
"${DOCKER_COMPOSE}" logs lrs_server | grep "up and running"

run_log "Check logs: envoy is connect to lrs_server"
responds_with \
    upstream_rq_200 \
    "http://localhost:${PORT_ADMIN}/stats?filter=load_reporting_cluster"

run_log "Check logs: lrs_server works normally"
"${DOCKER_COMPOSE}" logs lrs_server | grep "Got stats from cluster"

# TODO(phlax): add some test/docs for interacting with load reporting server
