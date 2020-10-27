#!/bin/bash -e

export NAME=skywalking
export DELAY=200

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test connection"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    http://localhost:8000/trace/1

run_log "Test stats"
responds_with \
    "tracing.skywalking.segments_sent: 1" \
    http://localhost:8001/stats

run_log "Test dashboard"
responds_with \
    "<!DOCTYPE html>" \
    http://localhost:8080

run_log "Test OAP Server"
responds_with \
    "getEndpoints" \
    http://localhost:8080/graphql \
    -X POST \
    -H "Content-Type:application/json" \
    -d "{ \"query\": \"query queryEndpoints(\$serviceId: ID!, \$keyword: String!) {
            getEndpoints: searchEndpoint(serviceId: \$serviceId, keyword: \$keyword, limit: 100) {
                key: id
                label: name
            }
          }\",
          \"variables\": { \"serviceId\": \"\", \"keyword\": \"\" }
        }"

responds_with \
    "currentTimestamp" \
    http://localhost:8080/graphql \
    -X POST \
    -H "Content-Type:application/json" \
    -d "{ \"query\": \"query queryOAPTimeInfo {
            getTimeInfo {
                timezone
                currentTimestamp
            }
          }\",
          \"variables\": {}
        }"
