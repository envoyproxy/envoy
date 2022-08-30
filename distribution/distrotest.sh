#!/bin/bash -E

FAILED=()
TESTNAME=


dump_envoy_response () {
    echo "Envoy did not respond correctly"
    echo "Response was"
    echo "$RESPONSE"
    echo
    echo "Log:"
    cat /tmp/envoy.log
}

dump_permissions () {
    echo "Actual permissions for: $1"
    stat -L -c "%a %G %U" "$1"
}

handle_fail () {
    run_log "${TESTNAME}" "ERROR"
    case "${TESTNAME}" in
        "proxy-responds")
            dump_envoy_response
            ;;
        "binary-permissions")
            dump_permissions /usr/bin/envoy
            ;;
        "config-permissions")
            dump_permissions /etc/envoy/envoy.yaml
            ;;
    esac
    return 1
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

retry () {
    local i=1 returns=1 seconds="$1"
    shift
    while ((i<=seconds)); do
        if "${@}" 2> /dev/null; then
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

trap trap_errors ERR
trap exit 1 INT

run_log package-sig "Check package signature"
$VERIFY_COMMAND "${ENVOY_INSTALLABLE}"

run_log package-maintainer "Check package maintainer"
$MAINTAINER_COMMAND | grep "$ENVOY_MAINTAINER"

run_log install-envoy "Install Envoy"
$INSTALL_COMMAND "${ENVOY_INSTALLABLE}" && echo "Envoy installed"

run_log group-exists "Check envoy group exists"
getent group envoy

run_log user-exists "Check envoy user exists"
getent passwd envoy

run_log shadow-no-password "Check envoy user has no password"
getent shadow envoy | grep -E '^envoy:!!:|^envoy:!:'

run_log user-in-group "Check envoy user is in envoy group"
sudo -u envoy groups | grep envoy

run_log user-home-dir "Check envoy user home directory"
getent passwd envoy | cut -d":" -f6 | grep "/nonexistent"

run_log user-shell "Check envoy user shell"
getent passwd envoy | cut -d":" -f7 | grep "/bin/false"

run_log binary-permissions "Check ownership/permissions of envoy binary"
test "$(stat -L -c "%a %G %U" /usr/bin/envoy)" == "$BINARY_PERMISSIONS" && echo "Correct permissions: ${BINARY_PERMISSIONS}"

run_log config-permissions "Check ownership/permissions of envoy config"
test "$(stat -L -c "%a %G %U" /etc/envoy/envoy.yaml)" == "$CONFIG_PERMISSIONS" && echo "Correct permissions: ${CONFIG_PERMISSIONS}"

run_log envoy-version "Envoy version: ${ENVOY_VERSION}"
envoy --version
envoy --version | grep "$ENVOY_VERSION"

run_log start-envoy "Start Envoy"
# shellcheck disable=SC2024
sudo -u envoy envoy -c /etc/envoy/envoy.yaml &> /tmp/envoy.log & echo "Envoy started"

run_log wait-for-envoy "Wait for Envoy starting"
sleep 2

run_log envoy-running "Check envoy is running"
pgrep envoy

run_log proxy-responds "Check proxy responds"
# The website can be flakey, give it a minute of trying...
RESPONSE="$(retry 60 curl -s http://localhost:10000/)"
echo "$RESPONSE" | grep "Envoy is an open source edge and service proxy, designed for cloud-native applications"

run_log stop-envoy "Stop envoy"
sudo -u envoy pkill envoy && echo "Envoy stopped"

run_log uninstall-envoy "Uninstall envoy"
$UNINSTALL_COMMAND "$PACKAGE"

run_log reinstall-envoy "Reinstall envoy"
$INSTALL_COMMAND "${ENVOY_INSTALLABLE}" && echo "Envoy reinstalled"

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    exit 1
fi
