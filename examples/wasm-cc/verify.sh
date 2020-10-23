#!/bin/bash -e

export NAME=wasm-cc

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
docker-compose stop proxy

run_log "Compile updated Wasm filter"
docker-compose -f docker-compose-wasm.yaml up --remove-orphans wasm_compile_update

run_log "Check for the compiled update"
ls -l lib/*updated*wasm

run_log "Edit the Docker recipe to use the updated binary"
sed -i s/\\.\\/lib\\/envoy_filter_http_wasm_example.wasm/.\\/lib\\/envoy_filter_http_wasm_updated_example.wasm/ Dockerfile-proxy

run_log "Bring the proxy back up"
docker-compose up --build -d proxy

run_log "Snooze for 5 while proxy starts"
sleep 5

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
