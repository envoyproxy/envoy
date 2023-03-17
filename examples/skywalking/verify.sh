#!/bin/bash -e

export NAME=skywalking
export PORT_PROXY="${SKYWALKING_PORT_PROXY:-12600}"
export PORT_UI="${SKYWALKING_PORT_UI:-12601}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Make a request to service-1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Make a request to service-2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/trace/2"

run_log "View the traces in Skywalking UI"
responds_with \
    "<!DOCTYPE html>" \
    "http://localhost:${PORT_UI}"

run_log "Test OAP Server"
responds_with \
    "getEndpoints" \
    "http://localhost:${PORT_UI}/graphql" \
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
    "http://localhost:${PORT_UI}/graphql" \
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
