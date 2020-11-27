#!/bin/bash -e

export NAME=fault-injection

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Send requests for 20 seconds"
docker-compose exec -T envoy bash -c \
               "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
    &> /dev/null

run_log "Check logs"
docker-compose logs | grep "HTTP/1.1\" 200"


_fault_injection_test () {
    local action code existing_200s existing_codes
    action="$1"
    code="$2"
    existing_codes=0

    # enable fault injection and check for http hits of type $code
    existing_codes=$(docker-compose logs | grep -c "HTTP/1.1\" ${code}" || :)
    run_log "Enable ${action} fault injection"
    docker-compose exec -T envoy bash "enable_${action}_fault_injection.sh"
    run_log "Send requests for 20 seconds"
    docker-compose exec -T envoy bash -c \
                   "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
        &> /dev/null
    run_log "Check logs again"
    new_codes=$(docker-compose logs | grep -c "HTTP/1.1\" ${code}")
    if [[ "$new_codes" -le "$existing_codes" ]]; then
        echo "ERROR: expected to find new logs with response code $code" >&2
        return 1
    fi

    # disable fault injection and check for http hits of type 200
    existing_200s=$(docker-compose logs | grep -c "HTTP/1.1\" 200")
    run_log "Disable ${action} fault injection"
    docker-compose exec -T envoy bash "disable_${action}_fault_injection.sh"
    run_log "Send requests for 20 seconds"
    docker-compose exec -T envoy bash -c \
                   "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
        &> /dev/null
    run_log "Check logs again"
    new_200s=$(docker-compose logs | grep -c "HTTP/1.1\" 200")
    if [[ "$new_200s" -le "$existing_200s" ]]; then
        echo "ERROR: expected to find new logs with response code 200" >&2
        return 1
    fi
}

_fault_injection_test abort 503
_fault_injection_test delay 200

run_log "Check tree"
docker-compose exec -T envoy tree /srv/runtime
