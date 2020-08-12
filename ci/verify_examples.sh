#!/bin/bash -E

TESTFILTER="${1:-*}"
FAILED=()
SRCDIR="${SRCDIR:-$(pwd)}"
EXCLUDED_BUILD_CONFIGS=${EXCLUDED_BUILD_CONFIGS:-"^./jaeger-native-tracing|docker-compose"}


trap_errors () {
    local frame=0 COMMAND LINE SUB FILE
    if [ -n "$example_test" ]; then
        COMMAND=" (${example_test})"
    fi
    set +v
    while read -r LINE SUB FILE < <(caller "$frame"); do
        if [ "$frame" -ne "0" ]; then
            FAILED+=("  > ${SUB}@ ${FILE} :${LINE}")
        else
            FAILED+=("${SUB}@ ${FILE} :${LINE}${COMMAND}")
        fi
        ((frame++))
    done
    set -v
}

trap trap_errors ERR
trap exit 1 INT

run_log () {
    local name
    name="$1"
    shift
    echo -e "\n> [${name}] ${*}"
}

show_user_env () {
    run_log "$(whoami)" "User env"
    id
    echo "umask = $(umask)"
    echo "pwd = $(pwd)"
}

get_path () {
    printf "%s/examples/%s" "$SRCDIR" "$1"
}

bring_up_example_stack () {
    local args name path snooze
    args=("${@}")
    name="$1"
    path="$2"
    snooze="${3:-0}"
    cd "$path" || return 1
    run_log "$name" "Pull the images"
    docker-compose pull || return 1
    echo
    run_log "$name" "Bring up services"
    docker-compose up --build -d "${args[@]:3}" || return 1
    if [ "$snooze" -ne "0" ]; then
        run_log "$name" "Snooze for ${snooze} while ${name} gets started"
        sleep "$snooze"
    fi
    docker-compose ps
    docker-compose logs
}

bring_up_example () {
    local name paths
    name="$1"
    read -ra paths <<< "$(echo "$2" | tr ',' ' ')"
    shift 2
    for path in "${paths[@]}"; do
        bring_up_example_stack "$name" "$(get_path "$path")" "$@"
    done
}

cleanup_stack () {
    local name path
    name="$1"
    path="$2"
    run_log "$name" "Cleanup: $path"
    cd "$path" || return 1
    docker-compose down
    docker system prune -f
}

cleanup () {
    local name paths
    name="$1"
    read -ra paths <<< "$(echo "$2" | tr ',' ' ')"
    for path in "${paths[@]}"; do
        cleanup_stack "$name" "$(get_path "$path")"
    done
}

run_example_cors () {
    local name paths
    name=cors
    paths="cors/frontend,cors/backend"
    bring_up_example "$name" "$paths"

    run_log "$name" "Test service"
    curl -s http://localhost:8000 | grep "Envoy CORS Webpage"

    run_log "$name" "Test cors server: disabled"
    curl -s -H "Origin: http://example.com" http://localhost:8002/cors/disabled \
        | grep Success
    curl -s --head -X GET \
         -H "Origin: http://example.com" \
         http://localhost:8002/cors/disabled \
        | grep access-control-allow-origin \
        | [ "$(wc -l)" -eq 0 ] || return 1

    run_log "$name" "Test cors server: open"
    curl -s -H "Origin: http://example.com" http://localhost:8002/cors/open \
        | grep Success
    curl -s --head -X GET \
         -H "Origin: http://example.com" \
         http://localhost:8002/cors/open \
        | grep "access-control-allow-origin: http://example.com"

    run_log "$name" "Test cors server: restricted"
    curl -s -H "Origin: http://example.com" http://localhost:8002/cors/restricted \
        | grep Success
    curl -s --head -X GET \
         -H "Origin: http://example.com" \
         http://localhost:8002/cors/restricted \
        | grep access-control-allow-origin \
        | [ "$(wc -l)" -eq 0 ] || return 1
    curl -s --head -X GET \
         -H "Origin: http://foo.envoyproxy.io" \
         http://localhost:8002/cors/restricted \
        | grep "access-control-allow-origin: http://foo.envoyproxy.io"
    cleanup "$name" "$paths"
}

run_example_csrf () {
    local name paths
    name=csrf
    paths="csrf/samesite,csrf/crosssite"

    bring_up_example "$name" "$paths"

    run_log "$name" "Test services"
    curl -s http://localhost:8002 | grep "Envoy CSRF Demo"
    curl -s http://localhost:8000 | grep "Envoy CSRF Demo"

    run_log "$name" "Test stats server"
    curl -s http://localhost:8001/stats | grep ":"

    run_log "$name" "Test csrf server: disabled"
    curl -s -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/disabled \
        | grep Success
    curl -s --head -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/disabled \
        | grep "access-control-allow-origin: http://example.com"

    run_log "$name" "Test csrf server: shadow"
    curl -s -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/shadow \
        | grep Success
    curl -s --head -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/shadow \
        | grep "access-control-allow-origin: http://example.com"

    run_log "$name" "Test csrf server: enabled"
    curl -s -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/enabled \
        | grep "Invalid origin"
    curl -s --head -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/enabled \
        | grep "HTTP/1.1 403 Forbidden"

    run_log "$name" "Test csrf server: additional_origin"
    curl -s -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/additional_origin \
        | grep Success
    curl -s --head -X POST \
         -H "Origin: http://example.com" \
         http://localhost:8000/csrf/additional_origin \
        | grep "access-control-allow-origin: http://example.com"

    cleanup "$name" "$paths"
}

run_example_ext_authz () {
    local name paths
    name=ext_authz
    paths=ext_authz

    bring_up_example "$name" "$paths"

    run_log "$name" "Test services responds with 403"
    curl -s --head -X GET \
         http://localhost:8000/service \
        | grep "HTTP/1.1 403 Forbidden"

    run_log "$name" "Restart front-envoy with FRONT_ENVOY_YAML=config/http-service.yaml"
    docker-compose down
    FRONT_ENVOY_YAML=config/http-service.yaml docker-compose up -d
    sleep 10

    run_log "$name" "Test service responds with 403"
    curl -s --head -X GET \
         http://localhost:8000/service \
        | grep "HTTP/1.1 403 Forbidden"

    run_log "$name" "Test authenticated service responds with 200"
    curl -s --head -X GET \
         -H "Authorization: Bearer token1" http://localhost:8000/service \
        | grep "HTTP/1.1 200 OK"

    run_log "$name" "Restart front-envoy with FRONT_ENVOY_YAML=config/opa-service/v2.yaml"
    docker-compose down
    FRONT_ENVOY_YAML=config/opa-service/v2.yaml docker-compose up -d
    sleep 10

    run_log "$name" "Test OPA service responds with 200"
    curl -s --head -X GET \
         http://localhost:8000/service \
        | grep "HTTP/1.1 200 OK"

    run_log "$name" "Check OPA logs"
    docker-compose logs ext_authz-opa-service | grep decision_id -A 30

    run_log "$name" "Check OPA service rejects POST"
    curl -s --head -X POST \
         http://localhost:8000/service \
        | grep "HTTP/1.1 403 Forbidden"

    cleanup "$name" "$paths"
}

_fault_injection_test () {
    local action code existing_200s existing_codes name
    action="$1"
    code="$2"
    name=fault_injection
    existing_codes=0

    # enable fault injection and check for http hits of type $code
    existing_codes=$(docker-compose logs | grep -c "HTTP/1.1\" ${code}" || :)
    run_log "$name" "Enable ${action} fault injection"
    docker-compose exec -T envoy bash "enable_${action}_fault_injection.sh"
    run_log "$name" "Send requests for 20 seconds"
    docker-compose exec -T envoy bash -c \
                   "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
        &> /dev/null
    run_log "$name" "Check logs again"
    new_codes=$(docker-compose logs | grep -c "HTTP/1.1\" ${code}")
    if [ "$new_codes" -le "$existing_codes" ]; then
        return 1
    fi

    # disable fault injection and check for http hits of type 200
    existing_200s=$(docker-compose logs | grep -c "HTTP/1.1\" 200")
    run_log "$name" "Disable ${action} fault injection"
    docker-compose exec -T envoy bash "disable_${action}_fault_injection.sh"
    run_log "$name" "Send requests for 20 seconds"
    docker-compose exec -T envoy bash -c \
                   "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
        &> /dev/null
    run_log "$name" "Check logs again"
    new_200s=$(docker-compose logs | grep -c "HTTP/1.1\" 200")
    if [ "$new_200s" -le "$existing_200s" ]; then
        return 1
    fi
}

run_example_fault_injection () {
    local name paths
    name=fault_injection
    paths=fault-injection

    bring_up_example "$name" "$paths"

    run_log "$name" "Send requests for 20 seconds"
    docker-compose exec -T envoy bash -c \
                   "bash send_request.sh & export pid=\$! && sleep 20 && kill \$pid" \
        &> /dev/null
    run_log "$name" "Check logs"
    docker-compose logs | grep "HTTP/1.1\" 200"

    _fault_injection_test abort 503
    _fault_injection_test delay 200

    run_log "$name" "Check tree"
    docker-compose exec -T envoy tree /srv/runtime

    cleanup "$name" "$paths"
}

run_example_front_proxy () {
    local name paths
    name=front_proxy
    paths=front-proxy
    bring_up_example "$name" "$paths"

    run_log "$name" "Test service: localhost:8080/service/1"
    curl -s http://localhost:8080/service/1 | grep Hello | grep "service 1"
    run_log "$name" "Test service: localhost:8080/service/2"
    curl -s http://localhost:8080/service/2 | grep Hello | grep "service 2"
    run_log "$name" "Test service: https://localhost:8443/service/1"
    curl -sk https://localhost:8443/service/1 | grep Hello | grep "service 1"

    run_log "$name" "Scale up docker service1=3"
    docker-compose scale service1=3
    run_log "$name" "Snooze for 5 while docker-compose scales..."
    sleep 5

    run_log "$name" "Test round-robin localhost:8080/service/1"
    docker-compose exec -T front-envoy bash -c "\
                   curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1 \
                   && curl -s http://localhost:8080/service/1" \
                   | grep Hello | grep "service 1"
    run_log "$name" "Test service inside front-envoy: localhost:8080/service/2"
    docker-compose exec -T front-envoy curl -s http://localhost:8080/service/2 | grep Hello | grep "service 2"
    run_log "$name" "Test service info: localhost:8080/server_info"
    docker-compose exec -T front-envoy curl http://localhost:8001/server_info | jq '.'
    run_log "$name" "Test service stats: localhost:8080/stats"
    docker-compose exec -T front-envoy curl http://localhost:8001/stats | grep ":"

    cleanup "$name" "$paths"
}

run_example_grpc_bridge () {
    local name paths
    name=grpc_bridge
    paths=grpc-bridge

    run_log "$name" "Generate protocol stubs"
    cd "$(get_path grpc-bridge)" || return 1
    docker-compose -f docker-compose-protos.yaml up
    docker container prune -f

    # shellcheck disable=SC2010
    ls -la client/kv/kv_pb2.py | grep kv_pb2.py
    # shellcheck disable=SC2010
    ls -la server/kv/kv.pb.go | grep kv.pb.go

    bring_up_example "$name" "$paths"

    run_log "$name" "Set key value foo=bar"
    docker-compose exec -T grpc-client /client/grpc-kv-client.py set foo bar | grep setf

    run_log "$name" "Get key foo"
    docker-compose exec -T grpc-client /client/grpc-kv-client.py get foo | grep bar

    cleanup "$name" "$paths"
}

run_example_jaeger_native_tracing () {
    local name paths
    name=jaeger_native
    paths=jaeger-native-tracing

    bring_up_example "$name" "$paths" 10

    run_log "$name" "Test services"
    curl -s http://localhost:8000/trace/1 | grep Hello

    run_log "$name" "Test Jaeger UI"
    curl -s http://localhost:16686 | grep "<!doctype html>"

    cleanup "$name" "$paths"
}

run_example_jaeger_tracing () {
    local name paths
    name=jaeger
    paths=jaeger-tracing

    bring_up_example "$name" "$paths"

    run_log "$name" "Test services"
    curl -s http://localhost:8000/trace/1 | grep Hello

    run_log "$name" "Test Jaeger UI"
    curl -s http://localhost:16686 | grep "<!doctype html>"

    cleanup "$name" "$paths"
}

run_example_load_reporting_service () {
    local name paths
    name=load_reporting
    paths=load-reporting-service

    bring_up_example "$name" "$paths" 0 --scale http_service=2

    run_log "$name" "Send requests"
    bash send_requests.sh 2> /dev/null
    run_log "$name" "Check logs: http 1"
    docker-compose logs http_service | grep http_service_1 | grep HTTP | grep 200

    run_log "$name" "Check logs: http 2"
    docker-compose logs http_service | grep http_service_2 | grep HTTP | grep 200

    run_log "$name" "Check logs: lrs_server"
    docker-compose logs lrs_server | grep "up and running"

    cleanup load_reporting "$paths"
}

run_example_lua () {
    local name paths
    name=lua
    paths=lua
    bring_up_example "$name" "$paths"

    run_log "$name" "Test connection"
    curl -s http://localhost:8000 | grep foo

    cleanup "$name" "$paths"
}

run_example_mysql () {
    local mysql_client name paths
    name=mysql
    paths=mysql
    mysql_client=(docker run --network envoymesh mysql:5.5 mysql -h envoy -P 1999 -u root)

    bring_up_example "$name" "$paths" 10

    run_log "$name" "Create a mysql database"
    "${mysql_client[@]}" -e "CREATE DATABASE test;"
    "${mysql_client[@]}" -e "show databases;"

    run_log "$name" "Create a mysql table"
    "${mysql_client[@]}" -e "USE test; CREATE TABLE test ( text VARCHAR(255) );"
    "${mysql_client[@]}" -e "SELECT COUNT(*) from test.test;"

    run_log "$name" "Check mysql egress stats"
    curl -s http://localhost:8001/stats?filter=egress_mysql | grep egress_mysql

    run_log "$name" "Check mysql TCP stats"
    curl -s http://localhost:8001/stats?filter=mysql_tcp | grep mysql_tcp

    cleanup "$name" "$paths"
}

run_example_redis () {
    local name paths
    name=redis
    paths=redis
    bring_up_example "$name" "$paths"

    run_log "$name" "Test set"
    redis-cli -h localhost -p 1999 set foo FOO | grep OK
    redis-cli -h localhost -p 1999 set bar BAR | grep OK

    run_log "$name" "Test get"
    redis-cli -h localhost -p 1999 get foo | grep FOO
    redis-cli -h localhost -p 1999 get bar | grep BAR

    run_log "$name" "Test redis stats"
    curl -s "http://localhost:8001/stats?usedonly&filter=redis.egress_redis.command" \
        | grep egress_redis

    cleanup "$name" "$paths"
}

run_example_zipkin_tracing () {
    local name paths
    name=zipkin
    paths=zipkin-tracing
    bring_up_example "$name" "$paths"

    run_log "$name" "Test connection"
    curl -s http://localhost:8000/trace/1 | grep Hello | grep "service 1"

    run_log "$name" "Test dashboard"
    # this could do with using the healthcheck and waiting
    sleep 20
    curl -s http://localhost:9411/zipkin/ | grep "<!doctype html>"

    cleanup "$name" "$paths"
}

run_examples () {
    local example examples example_test
    cd "${SRCDIR}/examples" || exit 1
    examples=$(find . -mindepth 1 -maxdepth 1 -type d -name "$TESTFILTER" | sort)
    for example in $examples; do
        example_test="run_example_$(echo "$example" | cut -d/ -f2 | tr '-' '_')"
        $example_test
    done
}

verify_build_configs () {
    local config configs missing
    missing=()
    cd "${SRCDIR}/examples" || return 1
    configs="$(find . -name "*.yaml" -o -name "*.lua" | grep -vE "${EXCLUDED_BUILD_CONFIGS}" | cut  -d/ -f2-)"
    for config in $configs; do
        grep "\"$config\"" BUILD || missing+=("$config")
    done
    if [ -n "${missing[*]}" ]; then
       for config in "${missing[@]}"; do
           echo "Missing config: $config" >&2
       done
       return 1
    fi
}

verify_build_configs
show_user_env
run_examples

if [ "${#FAILED[@]}" -ne "0" ]; then
    echo "TESTS FAILED:"
    for failed in "${FAILED[@]}"; do
        echo "$failed" >&2
    done
    exit 1
fi
