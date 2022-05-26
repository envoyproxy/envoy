#!/bin/bash -e

export NAME=locality-load-balancing
export DELAY=5

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

check_health() {
    docker-compose exec -T client-envoy curl -s localhost:8001/clusters | grep health_flags
}

check_backend() {
    output=$(docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100)
    echo "$output"
    for expected in "$@"
    do
        count=$(echo "$output" | grep -c "$expected" | xargs)
        if [ "$count" -eq 0 ]; then
            echo "Test fail: locality $expected is expected to be routed to."
            return 1
        fi
    done
}

bring_up_backend() {
    local server
    server="$1"

    docker-compose exec -T client-envoy curl -s "$server":8000/healthy
}

bring_down_backend() {
    local server
    server="$1"

    docker-compose exec -T client-envoy curl -s "$server":8000/unhealthy
}

run_log "=== Demo setup
client  -> backend-local-1      [priority: 0, weight: 1]
        -> backend-local-2      [priority: 1, weight: 1]
        -> backend-remote-1     [priority: 1, weight: 1]
        -> backend-remote-2     [priority: 2, weight: 1]
"

run_log "=== Scenario 1: one replica in the highest priority locality"

run_log "Send requests to backend."
check_health
check_backend backend-local-1

run_log "Bring down backend-local-1 then snooze for ${DELAY}s. Priority 0 locality is 0% healthy."
bring_down_backend "${NAME}"_backend-local-1_1
sleep ${DELAY}

run_log "Send requests to backend."
check_health
check_backend backend-local-2 backend-remote-1

run_log "Bring down backend-local-2 then snooze for ${DELAY}s. Priority 1 locality is 50% healthy."
bring_down_backend "${NAME}"_backend-local-2_1
sleep ${DELAY}

run_log "Traffic is load balanced goes to remote only."
check_health
check_backend backend-remote-1 backend-remote-2

run_log "=== Scenario 2: multiple replica in the highest priority locality"

run_log "Recover local-1 and local-2 then snooze for ${DELAY}s"
bring_up_backend "${NAME}"_backend-local-1_1
bring_up_backend "${NAME}"_backend-local-2_1
sleep ${DELAY}

run_log "Scale backend-local-1 to 5 replicas then snooze for ${DELAY}s"
docker-compose -p ${NAME} up --scale backend-local-1=5 -d --build
sleep ${DELAY}

run_log "Bring down 4 replicas in backend-local-1 then snooze for ${DELAY}s. Priority 0 locality is 20% healthy."
bring_down_backend "${NAME}"_backend-local-1_2
bring_down_backend "${NAME}"_backend-local-1_3
bring_down_backend "${NAME}"_backend-local-1_4
bring_down_backend "${NAME}"_backend-local-1_5
sleep ${DELAY}

run_log "Send requests to backend."
check_health
check_backend backend-local-1 backend-local-2 backend-remote-1

run_log "Bring down all endpoints of priority 1. Priority 1 locality is 0% healthy."
bring_down_backend "${NAME}"_backend-local-2_1
bring_down_backend "${NAME}"_backend-remote-1_1
sleep ${DELAY}

run_log "Send requests to backend."
check_health
check_backend backend-local-1 backend-remote-2
