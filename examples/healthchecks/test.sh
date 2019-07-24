#!/bin/bash
set -o nounset
set -o pipefail
set -o errexit
set -o errtrace


GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[96m'
CLEAR_COLOR='\033[0m'

FRONT_ENVOY_LISTENER="7500"
FRONT_ENVOY_ADMIN="7501"
SIDECAR_ENVOY_LISTENER="7502"
SIDECAR_ENVOY_ADMIN="7503"
HTTP_ECHO_PORT="7504"

function kill-processes {
    # Print color is set above so that when ports are killed, red means failed, green means
    # we passed tests and are cleaning up
    kill-envoy-pid "${FRONT_ENVOY_LISTENER}" "front envoy listener"
    kill-envoy-pid "${FRONT_ENVOY_ADMIN}" "front envoy admin"
    kill-envoy-pid "${SIDECAR_ENVOY_LISTENER}" "sidecar envoy listener"
    kill-envoy-pid "${SIDECAR_ENVOY_ADMIN}" "sidecar envoy admin"
    kill-envoy-pid "${HTTP_ECHO_PORT}" "http echo port"

    echo -e "** Trapped exit. Cleaned ports...${CLEAR_COLOR}"
}

function kill-envoy-pid {
    local port="${1}"
    local name="${2:-}"
    local portpid=$(lsof -nP -i4TCP:"${port}" -i6TCP:"${port}" | grep LISTEN | awk '{print $2}')
    if [ -n "${portpid}" ]; then
        echo -e "**** killing ${name} @ ${portpid} bound to ${port}"
        kill -9 "$portpid" > /dev/null 2>&1
    fi
}

trap kill-processes INT TERM EXIT
kill-processes

http-echo -listen ":${HTTP_ECHO_PORT}" -text "Hi i'm the server $(date)" > /dev/null 2>&1 &

# Docker version
# docker run --rm --network host -v $(pwd):/etc/envoy --entrypoint envoy envoyproxy/envoy:v1.11.0 -c /etc/envoy/front-envoy.yaml --mode validate
# docker run --rm --network host -v $(pwd):/etc/envoy --entrypoint envoy envoyproxy/envoy:v1.11.0 -c /etc/envoy/service.yaml --mode validate
# docker run --rm --network host -v $(pwd):/etc/envoy --entrypoint envoy envoyproxy/envoy:v1.11.0 -c /etc/envoy/front-envoy.yaml --base-id 100 > /dev/null 2>&1 &
# docker run --rm --network host -v $(pwd):/etc/envoy --entrypoint envoy envoyproxy/envoy:v1.11.0 -c /etc/envoy/service.yaml --base-id 101 > /dev/null 2>&1 &

envoy -c front-envoy.yaml --mode validate
envoy -c sidecar.yaml --mode validate
envoy -c front-envoy.yaml --base-id 100 > /dev/null 2>&1 &
envoy -c sidecar.yaml --base-id 101 > /dev/null 2>&1 &

sleep 5

echo "localhost:${FRONT_ENVOY_LISTENER} ================================"
curl -vvvv "localhost:${FRONT_ENVOY_LISTENER}"

echo "localhost:${SIDECAR_ENVOY_LISTENER} ================================"
curl -vvvv "localhost:${SIDECAR_ENVOY_LISTENER}"

echo "healthy ======================================="
while true; do
    echo "$(date): $(curl -sSLq localhost:${FRONT_ENVOY_ADMIN}/clusters | grep health_flags)"
    sleep 5
done
echo -e "ONLINE"
read -r -p ">>>> Press any key to exit..." -n1 -s
