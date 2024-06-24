#!/bin/bash -e

DELAY="${DELAY:-0}"
DOCKER_NO_PULL="${DOCKER_NO_PULL:-}"
MANUAL="${MANUAL:-}"
NAME="${NAME:-}"
PATHS="${PATHS:-.}"
UPARGS="${UPARGS:-}"
ENVOY_EXAMPLES_DEBUG="${ENVOY_EXAMPLES_DEBUG:-}"


if [[ -n "$DOCKER_COMPOSE" ]]; then
    read -ra DOCKER_COMPOSE <<< "$DOCKER_COMPOSE"
else
    DOCKER_COMPOSE=(docker compose)
fi

run_log () {
    echo -e "\n> [${NAME}] ${*}"
}

bring_up_example_stack () {
    local args path up_args
    args=("${UPARGS[@]}")
    path="$1"
    read -ra up_args <<< "up --quiet-pull --pull missing --build --wait -d ${args[*]}"

    if [[ -z "$DOCKER_NO_PULL" ]]; then
        run_log "Pull the images ($path)"
        "${DOCKER_COMPOSE[@]}" pull -q
        echo
    fi
    run_log "Bring up services ($path)"
    "${DOCKER_COMPOSE[@]}" "${up_args[@]}" || return 1

    if [[ -n "$ENVOY_EXAMPLES_DEBUG" ]]; then
        echo "----------------------------------------------"
        docker system df -v
        echo
        sudo du -ch / | grep "[0-9]G"
        echo
        df -h
        echo
        echo "----------------------------------------------"
    fi
    echo
}

bring_up_example () {
    local path paths
    read -ra paths <<< "$(echo "$PATHS" | tr ',' ' ')"

    for path in "${paths[@]}"; do
        pushd "$path" > /dev/null || return 1
        bring_up_example_stack "$path" || {
            echo "ERROR: starting ${NAME} ${path}" >&2
            return 1
        }
        popd > /dev/null || return 1
    done
    if [[ "$DELAY" -ne "0" ]]; then
        run_log "Snooze for ${DELAY} while ${NAME} gets started"
        sleep "$DELAY"
    fi
    for path in "${paths[@]}"; do
        pushd "$path" > /dev/null || return 1
        "${DOCKER_COMPOSE[@]}" ps
        "${DOCKER_COMPOSE[@]}" logs
        popd > /dev/null || return 1
    done
}

bring_down_example () {
    local path paths
    read -ra paths <<< "$(echo "$PATHS" | tr ',' ' ')"
    for path in "${paths[@]}"; do
        pushd "$path" > /dev/null || return 1
        cleanup_stack "$path" || {
            echo "ERROR: cleanup ${NAME} ${path}" >&2
        }
        popd > /dev/null
    done
}

cleanup_stack () {
    local path down_args
    path="$1"
    down_args=(--remove-orphans)

    if [[ -n "$DOCKER_RMI_CLEANUP" ]]; then
        down_args+=(--rmi all)
    fi

    # Remove sandbox volumes by default
    if [[ -z "$DOCKER_SAVE_VOLUMES" ]]; then
        down_args+=(--volumes)
    fi

    run_log "Cleanup ($path)"
    "${DOCKER_COMPOSE[@]}" down "${down_args[@]}"
}

debug_failure () {
    >&2 echo "FAILURE DEBUG"
    >&2 echo "DISK SPACE"
    df -h
    >&2 echo "DOCKER COMPOSE LOGS"
    "${DOCKER_COMPOSE[@]}" logs
    >&2 echo "DOCKER COMPOSE PS"
    "${DOCKER_COMPOSE[@]}" ps
}

cleanup () {
    local code="$?"

    if [[ "$code" -ne 0 ]]; then
        debug_failure
    fi

    bring_down_example

    if type -t finally &> /dev/null; then
        finally
    fi

    if [[ "$code" -ne 0 ]]; then
        run_log Failed
    else
        run_log Success
    fi
    echo
}

_curl () {
    local arg curl_command
    curl_command=(curl -s)
    if [[ ! "$*" =~ "-X" ]]; then
        curl_command+=(-X GET)
    fi
    for arg in "${@}"; do
        curl_command+=("$arg")
    done
    "${curl_command[@]}" || {
        echo "ERROR: curl (${curl_command[*]})" >&2
        return 1
    }
}

move_if_exists () {
    if [ -e "$1" ]; then
        mv "$1" "$2"
    else
        echo "Warning: $1 does not exist. Skipping move operation."
    fi
}

responds_with () {
    local expected response
    expected="$1"
    shift
    response=$(_curl "${@}")
    grep -Fs "$expected" <<< "$response" || {
        echo "ERROR: curl (${*})" >&2
        echo "EXPECTED:" >&2
        echo "$expected" >&2
        echo "RECEIVED:" >&2
        echo "$response" >&2
        return 1
    }
}

responds_without () {
    local expected response
    expected="$1"
    shift
    response=$(_curl "${@}")
    # shellcheck disable=2266
    grep -s "$expected" <<< "$response" | [[ "$(wc -l)" -eq 0 ]] || {
        echo "ERROR: curl (${*})" >&2
        echo "DID NOT EXPECT: $expected" >&2
        echo "RECEIVED:" >&2
        echo "$response" >&2
        return 1
    }
}

responds_with_header () {
    local expected response
    expected="$1"
    shift
    response=$(_curl --head "${@}")
    grep -s "$expected" <<< "$response"  || {
        echo "ERROR: curl (${*})" >&2
        echo "EXPECTED HEADER:" >&2
        echo "$expected" >&2
        echo "RECEIVED:" >&2
        echo "$response" >&2
        return 1
    }
}

responds_without_header () {
    local expected response
    expected="$1"
    shift
    response=$(_curl --head "${@}")
    # shellcheck disable=2266
    grep -s "$expected" <<< "$response" | [[ "$(wc -l)" -eq 0 ]] || {
        echo "ERROR: curl (${*})" >&2
        echo "DID NOT EXPECT HEADER: $expected" >&2
        echo "RECEIVED:" >&2
        echo "$response" >&2
        return 1
    }
}

wait_for () {
    local i=1 returns=1 seconds="$1"
    shift
    while ((i<=seconds)); do
        if "${@}" &> /dev/null; then
            returns=0
            break
        else
            sleep 1
            ((i++))
        fi
    done
    if [[ "$returns" != 0 ]]; then
        echo "Wait (${seconds}) failed: ${*}" >&2
    fi
    return "$returns"
}

trap 'cleanup' EXIT

if [[ -z "$NAME" ]]; then
    echo "ERROR: You must set the '$NAME' variable before sourcing this script" >&2
    exit 1
fi

if [[ -z "$MANUAL" ]]; then
    bring_up_example
fi


# These allow the functions to be used in subshells, e.g. in `wait_for`
export -f responds_with
export -f responds_without
export -f responds_with_header
export -f responds_without_header
export -f _curl
