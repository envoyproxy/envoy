#!/bin/bash -E

BINARY="${1}"
PACKAGE="${2}"
DISTRO="${3}"
FAILED=()
TESTNAME=


dump_envoy_response () {
    echo "Envoy did not respond correctly"
    echo "Response was"
    echo "$RESPONSE"
    echo
    echo "Log"
    cat /tmp/envoy.log
    return 1
}

handle_fail () {
    run_log "${TESTNAME}" "ERROR"
    case "${TESTNAME}" in
        "proxy-responds")
            dump_envoy_response
            ;;
    esac
}


trap_errors () {
    if [[ -n "$TESTNAME" ]]; then
        handle_fail
        FAILED+=("$TESTNAME")
    fi
}

run_log () {
    TESTNAME="$1"
    shift
    echo -e "[${DISTRO}/${PACKAGE}:${TESTNAME}] ${*}"
}

trap trap_errors ERR
trap exit 1 INT

run_log install-envoy "Install Envoy"
$INSTALL_COMMAND "${BINARY}"

run_log group-exists "Check envoy group exists"
getent group | grep envoy

run_log user-exists "Check envoy user exists"
getent passwd | grep envoy

run_log shadow-no-password "Check envoy user has no password"
getent shadow | grep envoy | grep -E '^envoy:!!:|^envoy:!:'

run_log user-in-group "Check envoy user is in envoy group"
sudo -u envoy groups | grep envoy

run_log user-home-dir "Check envoy user home directory"
getent passwd envoy | cut -d":" -f6 | grep "/nonexistent"

run_log user-shell "Check envoy user shell"
getent passwd envoy | cut -d":" -f7 | grep "/bin/false"

run_log package-maintainers "Check package maintainers"
$MAINTAINER_COMMAND "$PACKAGE"

run_log envoy-version "Envoy version"
envoy --version

run_log start-envoy "Start Envoy"
# shellcheck disable=SC2024
sudo -u envoy envoy -c /etc/envoy/envoy.yaml &> /tmp/envoy.log &

run_log wait-for-envoy "Wait for Envoy starting"
sleep 2

run_log envoy-running "Check envoy is running"
pgrep envoy

run_log proxy-responds "Check proxy responds"
RESPONSE=$(curl -s http://localhost:10000/)
echo  "$RESPONSE" | grep "Welcome to Envoy"

run_log stop-envoy "Stop envoy"
sudo -u envoy pkill envoy

run_log uninstall-envoy "Uninstall envoy"

echo "$UNINSTALL_COMMAND" "$PACKAGE"

$UNINSTALL_COMMAND "$PACKAGE"

run_log reinstall-envoy "Reinstall envoy"
$INSTALL_COMMAND "${BINARY}"

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    exit 1
fi
