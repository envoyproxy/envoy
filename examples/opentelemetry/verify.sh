#!/bin/bash -e

export NAME=opentelemetry
export PORT_PROXY="${OPENTELEMETRY_PORT_PROXY:-12000}"
export PORT_UI="${OPENTELEMETRY_PORT_UI:-12001}"

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

run_log "View the traces in OpenTelemetry UI"
responds_with \
    "<!DOCTYPE html>" \
    "http://localhost:${PORT_UI}/debug/tracez"

# Wait until the collector has outputted all access logs: we expect 4, 2 for each request issued to frontend proxy
wait_for 10 bash -c "docker compose exec opentelemetry cat /tmp/logs.json | jq --slurp -r -c '.[] | .resourceLogs[] | .scopeLogs[] | .logRecords[] | {traceId: .traceId, spanId: .spanId}' | wc -l | xargs -I{} [ "{}" = 4 ]"

logs=$(docker compose exec opentelemetry cat /tmp/logs.json | jq --slurp -r -c '.[] | .resourceLogs[] | .scopeLogs[] | .logRecords[] | {traceId: .traceId, spanId: .spanId}')

while read log; do
    if ! wait_for 10 bash -c "docker compose exec opentelemetry cat /tmp/spans.json | jq --slurp -r -c '.[] | .resourceSpans[] | .scopeSpans[] | .spans[] | {traceId: .traceId, spanId: .spanId}' | grep -Fxq \"${log}\""; then
        run_log "Cannot find span with trace context: ${log}"
        run_log "Spans found: $(docker compose exec opentelemetry cat /tmp/spans.json | jq --slurp -r -c '.[] | .resourceSpans[] | .scopeSpans[] | .spans[] | {traceId: .traceId, spanId: .spanId}')"
        exit 1
    fi
done <<< "${logs}"