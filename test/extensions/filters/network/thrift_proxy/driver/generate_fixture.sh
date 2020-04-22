#!/bin/bash

# Generates request and response fixtures for integration tests.

# Usage: generate_fixture.sh <transport> <protocol> -s [multiplex-service] -H [headers] method [param...]

set -e

function usage() {
    echo "Usage: $0 <mode> <transport> <protocol> -s [multiplex-service] -H [headers] method [param...]"
    echo "where mode is success, exception, or idl-exception"
    exit 1
}

FIXTURE_DIR="${TEST_TMPDIR}"
mkdir -p "${FIXTURE_DIR}"

DRIVER_DIR="${TEST_SRCDIR}/envoy/test/extensions/filters/network/thrift_proxy/driver"

if [[ -z "${TEST_UDSDIR}" ]]; then
    TEST_UDSDIR=`mktemp -d /tmp/envoy_test_thrift.XXXXXX`
fi

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
while getopts ":s:H:" opt; do
    case ${opt} in
        s)
            MULTIPLEX=$OPTARG
            ;;
        H)
            HEADERS=$OPTARG
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

SOCKET="${TEST_UDSDIR}/fixture.sock"
rm -f "${SOCKET}"

SERVICE_FLAGS=("--addr" "${SOCKET}"
               "--unix"
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
"${DRIVER_DIR}/server" "${SERVICE_FLAGS[@]}" &
SERVER_PID="$!"

trap "kill ${SERVER_PID}" EXIT;

while [[ ! -a "${SOCKET}" ]]; do
    sleep 0.1

    if ! kill -0 "${SERVER_PID}"; then
        echo "server failed to start"
        exit 1
    fi
done

if [[ -n "$HEADERS" ]]; then
    SERVICE_FLAGS+=("--headers")
    SERVICE_FLAGS+=("$HEADERS")
fi

"${DRIVER_DIR}/client" "${SERVICE_FLAGS[@]}" \
                       --request "${REQUEST_FILE}" \
                       --response "${RESPONSE_FILE}" \
                       "${METHOD}" "$@"
