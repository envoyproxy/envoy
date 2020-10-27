#!/bin/bash -e

DELAY="${DELAY:-0}"
DOCKER_NO_PULL="${DOCKER_NO_PULL:-}"
MANUAL="${MANUAL:-}"
NAME="${NAME:-}"
PATHS="${PATHS:-.}"
UPARGS="${UPARGS:-}"


run_log () {
    echo -e "\n> [${NAME}] ${*}"
}

bring_up_example_stack () {
    local args path up_args
    args=("${UPARGS[@]}")
    path="$1"
    read -ra up_args <<< "up --build -d ${args[*]}"
    if [[ -z "$DOCKER_NO_PULL" ]]; then
	run_log "Pull the images ($path)"
	docker-compose pull
	echo
    fi
    run_log "Bring up services ($path)"
    docker-compose "${up_args[@]}" || return 1
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
        docker-compose ps
        docker-compose logs
        popd > /dev/null || return 1
    done
}

cleanup_stack () {
    local path
    path="$1"
    run_log "Cleanup ($path)"
    docker-compose down
}

cleanup () {
    local path paths
    read -ra paths <<< "$(echo "$PATHS" | tr ',' ' ')"
    for path in "${paths[@]}"; do
        pushd "$path" > /dev/null || return 1
        cleanup_stack "$path" || {
            echo "ERROR: cleanup ${NAME} ${path}" >&2
            return 1
        }
        popd > /dev/null
    done
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

responds_with () {
    local expected
    expected="$1"
    shift
    _curl "${@}" | grep "$expected" || {
        echo "ERROR: curl expected (${*}): $expected" >&2
        return 1
    }
}

responds_with_header () {
    local expected
    expected="$1"
    shift
    _curl --head "${@}" | grep "$expected"  || {
        echo "ERROR: curl header (${*}): $expected" >&2
        return 1
    }
}

responds_without_header () {
    local expected
    expected="$1"
    shift
    _curl --head "${@}" | grep "$expected" | [[ "$(wc -l)" -eq 0 ]] || {
        echo "ERROR: curl without header (${*}): $expected" >&2
        return 1
    }
}

wait_for () {
    local i=1 returns=1 seconds="$1"
    shift
    while ((i<=seconds)); do
	if "$@"; then
	    returns=0
	    break
	else
	    sleep 1
	    ((i++))
	fi
    done
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
