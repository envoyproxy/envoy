#!/bin/bash -e

export NAME=wasm-cc
export UID


# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test connection"
responds_with \
    "Hello, world" \
    http://localhost:8000

run_log "Test content-type header"
responds_with_header \
    "content-type: text/plain" \
    http://localhost:8000

run_log "Test custom Wasm header"
responds_with_header \
    "x-wasm-custom: FOO" \
    http://localhost:8000

run_log "Bring down the proxy"
"${DOCKER_COMPOSE[@]}" stop proxy

run_log "Compile updated Wasm filter"
"${DOCKER_COMPOSE[@]}" -f docker-compose-wasm.yaml up --quiet-pull --remove-orphans wasm_compile_update

run_log "Check for the compiled update"
ls -l lib/*updated*wasm

run_log "Edit the Docker recipe to use the updated binary"
sed -i'.bak' s/\\.\\/lib\\/envoy_filter_http_wasm_example.wasm/.\\/lib\\/envoy_filter_http_wasm_updated_example.wasm/ Dockerfile-proxy

run_log "Bring the proxy back up"
"${DOCKER_COMPOSE[@]}" up --build -d proxy
wait_for 10 bash -c "\
         responds_with \
         'Hello, Wasm world' \
         http://localhost:8000"

run_log "Test updated connection"
responds_with \
    "Hello, Wasm world" \
    http://localhost:8000

run_log "Test updated content-type header"
responds_with_header \
    "content-type: text/html" \
    http://localhost:8000

run_log "Test updated Wasm header"
responds_with_header \
    "x-wasm-custom: BAR" \
    http://localhost:8000

# Restore original Dockerfile
mv Dockerfile-proxy.bak Dockerfile-proxy
