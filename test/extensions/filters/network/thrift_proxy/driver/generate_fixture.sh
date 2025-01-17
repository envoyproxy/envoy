#!/usr/bin/env bash

# Generates request and response fixtures for integration tests.

# Usage: generate_fixture.sh <transport> <protocol> -s [multiplex-service] -H [headers] method [param...]

set -e

function usage() {
    echo "Usage: $0 <mode> <transport> <protocol> -s [multiplex-service] -H [headers] -T [TempPath] method [param...]"
    echo "where mode is success, exception, or idl-exception"
    exit 1
}

MODE="$1"
TRANSPORT="$2"
PROTOCOL="$3"

if ! shift 3; then
    usage
fi

if [[ -z "${MODE}" || -z "${TRANSPORT}" || -z "${PROTOCOL}" ]]; then
    usage
fi

MULTIPLEX=
HEADERS=
TEST_TMPDIR=
while getopts ":s:H:T:" opt; do
    case ${opt} in
        s)
            MULTIPLEX=$OPTARG
            ;;
        H)
            HEADERS=$OPTARG
            ;;
        T)
            TEST_TMPDIR=$OPTARG
            ;;
        \?)
            echo "Invalid Option: -$OPTARG" >&2
            exit 1
            ;;
        :)
            echo "Invalid Option: -$OPTARG requires an argument" >&2
            exit 1
            ;;
    esac
done
shift $((OPTIND -1))

METHOD="$1"
if [[ "${METHOD}" == "" ]]; then
    usage
fi
shift

FIXTURE_DIR="${TEST_TMPDIR}"
mkdir -p "${FIXTURE_DIR}"

DRIVER_DIR="${TEST_SRCDIR}/envoy/test/extensions/filters/network/thrift_proxy/driver"

# On UNIX python supports AF_UNIX socket which are more reliable and efficient for communication
# between the client and the server, so we use it. On Windows, we find a random unused port
# on and let the communication happen via TCP.
SOCKET=""
if [[ "$OSTYPE" == "msys" ]]; then
    while
        port=$(shuf -n 1 -i 49152-65535)
        netstat -atn | grep -q "$port" >> /dev/null
    do
        echo "Port is used. retrying..."
        continue
    done
    SOCKET="127.0.0.1:${port}"
else
    if [[ -z "${TEST_UDSDIR}" ]]; then
        TEST_UDSDIR=$(mktemp -d /tmp/envoy_test_thrift.XXXXXX)
    fi
    SOCKET="${TEST_UDSDIR}/fixture.sock"
    rm -f "${SOCKET}"
fi

echo "Using address ${SOCKET}"
SERVICE_FLAGS=("--addr" "${SOCKET}"
               "--response" "${MODE}"
               "--transport" "${TRANSPORT}"
               "--protocol" "${PROTOCOL}")

if [[ -n "$MULTIPLEX" ]]; then
    SERVICE_FLAGS+=("--multiplex")
    SERVICE_FLAGS+=("${MULTIPLEX}")

    REQUEST_FILE="${FIXTURE_DIR}/${TRANSPORT}-${PROTOCOL}-${MULTIPLEX}-${MODE}.request"
    RESPONSE_FILE="${FIXTURE_DIR}/${TRANSPORT}-${PROTOCOL}-${MULTIPLEX}-${MODE}.response"
else
    REQUEST_FILE="${FIXTURE_DIR}/${TRANSPORT}-${PROTOCOL}-${MODE}.request"
    RESPONSE_FILE="${FIXTURE_DIR}/${TRANSPORT}-${PROTOCOL}-${MODE}.response"
fi

# start server
if [[ "$OSTYPE" == "msys" ]]; then
    echo "${SERVICE_FLAGS[@]}"
    "${DRIVER_DIR}/server.exe" "${SERVICE_FLAGS[@]}" &
    SERVER_PID="$!"
else
    SERVICE_FLAGS+=("--unix")
    "${DRIVER_DIR}/server" "${SERVICE_FLAGS[@]}" &
    SERVER_PID="$!"
    while [[ ! -a "${SOCKET}" ]]; do
        sleep 0.1

        if ! kill -0 "${SERVER_PID}"; then
            echo "server failed to start"
            exit 1
        fi
    done
fi

trap 'kill ${SERVER_PID}' EXIT;

CLIENT_COMMAND=""
if [[ "$OSTYPE" == "msys" ]]; then
    CLIENT_COMMAND="${DRIVER_DIR}/client.exe"
else
    CLIENT_COMMAND="${DRIVER_DIR}/client"
fi

if [[ -n "$HEADERS" ]]; then
    SERVICE_FLAGS+=("--headers")
    SERVICE_FLAGS+=("$HEADERS")
fi

echo  "${METHOD}" "$@"

$CLIENT_COMMAND "${SERVICE_FLAGS[@]}" \
                --request "${REQUEST_FILE}" \
                --response "${RESPONSE_FILE}" \
                "${METHOD}" "$@"
