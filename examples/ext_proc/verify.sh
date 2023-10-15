#!/bin/bash -e

export NAME=ext_proc
export PORT_PROXY="${EXT_AUTH_PORT_PROXY:-10000}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services responds with 403"
responds_with_header \
    "HTTP/1.1 403 Forbidden"\
    "http://localhost:${PORT_PROXY}/service"

run_log "Restart front-envoy with FRONT_ENVOY_YAML=config/http-service.yaml"
"${DOCKER_COMPOSE[@]}" down
FRONT_ENVOY_YAML=config/http-service.yaml "${DOCKER_COMPOSE[@]}" up -d

wait_for 15 bash -c "\
         responds_with_header \
         'HTTP/1.1 200 OK' \
         -H 'Authorization: Bearer token1' \
         http://localhost:${PORT_PROXY}/service"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Testing ext_proc Go processor API endpoints"

# Test /help endpoint
responds_with_body "Help API" "http://localhost:${PORT_PROXY}/help"

# Test /hello endpoint
responds_with_body "Hello, World!" "http://localhost:${PORT_PROXY}/hello"

# Test /json endpoint
responds_with_body '{"key": "value"}' "http://localhost:${PORT_PROXY}/json"

# Test /echo endpoint
responds_with_body "Request Body" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/echo"

# Test /addHeader endpoint
responds_with_header "x-external-processor-status: Added" "http://localhost:${PORT_PROXY}/addHeader"

# Test /echohashstream endpoint
responds_with_body "Request Body" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/echohashstream"

# Test /echohashbuffered endpoint
responds_with_body "Request Body" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/echohashbuffered"

# Test /echohashbufferedpartial endpoint
responds_with_body "Request Body" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/echohashbufferedpartial"

# Test /echoencode endpoint
responds_with_body "Request Body" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/echoencode"

# Test /checkJson endpoint
responds_with_body "Request Body" -H "Content-Type: application/json" -X POST -d "Request Body" "http://localhost:${PORT_PROXY}/checkJson"

# Test /getToPost endpoint
responds_with_body '{"message": "This was a GET request converted to POST"}' "http://localhost:${PORT_PROXY}/getToPost"

# Test /notfound endpoint
responds_with_status "404" "http://localhost:${PORT_PROXY}/notfound"

run_log "All tests passed successfully"

# Exit with status code 0 to indicate successful tests
exit 0

