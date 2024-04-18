#!/bin/bash -e
#
# Requirements: expect
#

export NAME=websocket
export MANUAL=true
export PORT_PROXY0="${WEBSOCKET_PORT_PROXY0:-12300}"
export PORT_PROXY1="${WEBSOCKET_PORT_PROXY1:-12301}"
export PORT_PROXY2="${WEBSOCKET_PORT_PROXY2:-12302}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# TODO(phlax): remove openssl bug workaround when openssl/ubuntu are updated
#    see #15555 for more info
touch ~/.rnd

run_log "Generate wss certs"
mkdir -p certs
openssl req -batch -new -x509 -nodes -keyout certs/key.pem -out certs/cert.pem
openssl pkcs12 -export -passout pass: -out certs/output.pkcs12 -inkey certs/key.pem -in certs/cert.pem

UPARGS="proxy-ws proxy-wss-wss proxy-wss-passthrough service-ws service-wss"

bring_up_example

run_log "Interact with web socket ws -> ws"
"${DOCKER_COMPOSE[@]}" run client-ws "${PORT_PROXY0}" ws ws

run_log "Interact with web socket wss -> wss"
"${DOCKER_COMPOSE[@]}" run client-ws "${PORT_PROXY1}" wss wss

run_log "Interact with web socket wss passthrough"
"${DOCKER_COMPOSE[@]}" run client-ws "${PORT_PROXY2}" wss wss
