#!/bin/bash -e

export NAME=locality-load-balancing


# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


dump_clusters () {
    "${DOCKER_COMPOSE[@]}" exec -T client-envoy curl -s "localhost:8001/clusters"
}

check_health() {
    local ip_address
    ip_address="$(docker inspect --format '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "${NAME}-${1}")"
    dump_clusters  | grep "backend::${ip_address}:8080::health_flags::${2}"
}

check_backend() {
    output=$("${DOCKER_COMPOSE[@]}" exec -T client-envoy python3 client.py http://localhost:3000/ 100)
    echo "$output"
    for expected in "$@"; do
        count=$(echo "$output" | grep -c "$expected" | xargs)
        if [ "$count" -eq 0 ]; then
            echo "Test fail: locality $expected is expected to be routed to."
            return 1
        fi
    done
}

make_healthy() {
    "${DOCKER_COMPOSE[@]}" exec -T client-envoy curl -s "${NAME}-${1}:8080/healthy"
    wait_for 5 check_health "${1}" healthy
}

make_unhealthy() {
    "${DOCKER_COMPOSE[@]}" exec -T client-envoy curl -s "${NAME}-${1}:8080/unhealthy"
    wait_for 5 check_health "${1}" /failed_active_hc
}

run_log "Wait for backend clusters to become healthy."
wait_for 5 check_health backend-local-1-1 healthy
wait_for 5 check_health backend-local-2-1 healthy
wait_for 5 check_health backend-remote-1-1 healthy
wait_for 5 check_health backend-remote-2-1 healthy

run_log "Dump configured Envoy clusters"
dump_clusters

run_log "=== Demo setup
client  -> backend-local-1      [priority: 0, weight: 1]
        -> backend-local-2      [priority: 1, weight: 1]
        -> backend-remote-1     [priority: 1, weight: 1]
        -> backend-remote-2     [priority: 2, weight: 1]
"

run_log "=== Scenario 1: one replica in the highest priority locality"

run_log "Send requests to backend."
wait_for 5 check_health backend-local-1-1 healthy
check_backend backend-local-1

run_log "Bring down backend-local-1. Priority 0 locality is 0% healthy."
make_unhealthy backend-local-1-1

run_log "Send requests to backend."
check_backend backend-local-2 backend-remote-1

run_log "Bring down backend-local-2. Priority 1 locality is 50% healthy."
make_unhealthy backend-local-2-1

run_log "Traffic is load balanced goes to remote only."
check_backend backend-remote-1 backend-remote-2

run_log "=== Scenario 2: multiple replica in the highest priority locality"
run_log "Recover local-1 and local-2"
make_healthy backend-local-1-1
make_healthy backend-local-2-1

run_log "Scale backend-local-1 to 5 replicas."
"${DOCKER_COMPOSE[@]}" -p "${NAME}" up --scale backend-local-1=5 -d --build
wait_for 5 check_health backend-local-1-2 healthy
wait_for 5 check_health backend-local-1-3 healthy
wait_for 5 check_health backend-local-1-4 healthy
wait_for 5 check_health backend-local-1-5 healthy

run_log "Bring down 4 replicas in backend-local-1. Priority 0 locality is 20% healthy."
make_unhealthy backend-local-1-2
make_unhealthy backend-local-1-3
make_unhealthy backend-local-1-4
make_unhealthy backend-local-1-5

run_log "Send requests to backend."
check_backend backend-local-1 backend-local-2 backend-remote-1

run_log "Bring down all endpoints of priority 1. Priority 1 locality is 0% healthy."
make_unhealthy backend-local-2-1
make_unhealthy backend-remote-1-1

run_log "Send requests to backend."
check_backend backend-local-1 backend-remote-2
