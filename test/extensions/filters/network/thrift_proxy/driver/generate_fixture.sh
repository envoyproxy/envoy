#!/bin/bash

# Generates request and response fixtures for integration tests.

# Usage: generate_fixture.sh <transport> <protocol> [multiplex-service] -- method [param...]

set -e

function usage() {
    echo "Usage: $0 <mode> <transport> <protocol> [multiplex-service] -- method [param...]"
    echo "where mode is success, exception, or idl-exception"
    exit 1
}

FIXTURE_DIR="${TEST_TMPDIR}"
mkdir -p "${FIXTURE_DIR}"

DRIVER_DIR="${TEST_RUNDIR}/test/extensions/filters/network/thrift_proxy/driver"

if [[ -z "${TEST_UDSDIR}" ]]; then
    TEST_UDSDIR=`mktemp -d /tmp/envoy_test_thrift.XXXXXX`
fi

MODE="$1"
TRANSPORT="$2"
PROTOCOL="$3"
MULTIPLEX="$4"
if ! shift 4; then
    usage
fi

if [[ -z "${MODE}" || -z "${TRANSPORT}" || -z "${PROTOCOL}" || -z "${MULTIPLEX}" ]]; then
    usage
fi

if [[ "${MULTIPLEX}" != "--" ]]; then
    if [[ "$1" != "--" ]]; then
        echo "expected -- after multiplex service name"
        exit 1
    fi
    shift
else
    MULTIPLEX=""
fi

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
    SERVICE_FLAGS[9]="--multiplex"
    SERVICE_FLAGS[10]="${MULTIPLEX}"

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

"${DRIVER_DIR}/client" "${SERVICE_FLAGS[@]}" \
                       --request "${REQUEST_FILE}" \
                       --response "${RESPONSE_FILE}" \
                       "${METHOD}" "$@"
