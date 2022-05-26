#!/bin/bash -e

export NAME=load-reporting
export UPARGS="--scale http_service=2"
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Send requests"
bash send_requests.sh 2> /dev/null
run_log "Check logs: http 1"
docker-compose logs http_service | grep http_service_1 | grep HTTP | grep 200

run_log "Check logs: http 2"
docker-compose logs http_service | grep http_service_2 | grep HTTP | grep 200

run_log "Check logs: lrs_server"
docker-compose logs lrs_server | grep "up and running"

run_log "Check logs: envoy is connect to lrs_server"
responds_with \
    upstream_rq_200 \
    "http://localhost:8081/stats?filter=load_reporting_cluster"

run_log "Check logs: lrs_server works normally"
docker-compose logs lrs_server | grep "Got stats from cluster"

# TODO(phlax): add some test/docs for interacting with load reporting server
