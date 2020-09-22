#!/bin/bash

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


while
  port=$(shuf -n 1 -i 49152-65535)
  netstat -atn | grep -q "$port" >> /dev/null
do
  continue
done


SOCKET="127.0.0.1:${port}"
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
"${DRIVER_DIR}/server.py" "${SERVICE_FLAGS[@]}" &
SERVER_PID="$!"

trap 'kill ${SERVER_PID}' EXIT;


if [[ -n "$HEADERS" ]]; then
    SERVICE_FLAGS+=("--headers")
    SERVICE_FLAGS+=("$HEADERS")
fi

echo  "${METHOD}" "$@"

"${DRIVER_DIR}/client.py" "${SERVICE_FLAGS[@]}" \
                       --request "${REQUEST_FILE}" \
                       --response "${RESPONSE_FILE}" \
                       "${METHOD}" "$@"
