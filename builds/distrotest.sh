#!/bin/bash -E

DISTRO="${DISTRO}"
PACKAGE="${PACKAGE}"
FAILED=()
TESTNAME=

trap_errors () {
    local frame=0 command line sub file
    if [[ -n "$TESTNAME" ]]; then
        command=" (${TESTNAME})"
    fi
    set +v
    while read -r line sub file < <(caller "$frame"); do
        if [[ "$frame" -ne "0" ]]; then
            FAILED+=("  > ${sub}@ ${file} :${line}")
        else
            FAILED+=("${sub}@ ${file} :${line}${command}")
        fi
        ((frame++))
    done
    set -v
}

run_log () {
    local msg
    TESTNAME="$1"
    shift
    echo -e "\n[${DISTRO}($PACKAGE):${TESTNAME}] ${*}"
}

trap trap_errors ERR
trap exit 1 INT

run_log install-envoy "Install Envoy"
$INSTALL_COMMAND

run_log group-exists "Check envoy group exists"
getent group | grep envoy

run_log user-exists "Check envoy user exists"
getent passwd | grep envoy

run_log shadow-no-password "Check envoy user has no password"
getent shadow | grep envoy | grep -E '^envoy:!!:|^envoy:!:'

run_log user-in-group "Check envoy user is in envoy group"
sudo -u envoy groups | grep envoy

run_log user-home-dir "Check envoy home directory"
getent passwd envoy | cut -d":" -f6 | grep "/nonexistent"

run_log user-shell "Check envoy shell"
getent passwd envoy | cut -d":" -f7 | grep "/bin/false"

run_log start-envoy "Start Envoy"
sudo -u envoy envoy -c /etc/envoy/envoy.yaml &> /dev/null &

run_log wait-for-envoy "Wait for Envoy starting"
sleep 5

run_log envoy-running "Check envoy is running"
pgrep envoy

run_log proxy-responds "Check proxy responds"
curl -s http://localhost:10000 | grep "I'm Feeling Lucky"

run_log stop-envoy "Stop envoy"
sudo -u envoy pkill envoy

run_log uninstall-envoy "Uninstall envoy"

echo "$UNINSTALL_COMMAND"

$UNINSTALL_COMMAND

run_log reinstall-envoy "Reinstall envoy"
$INSTALL_COMMAND

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo -e "\nTESTS FAILED:" >&2
    for failed in "${FAILED[@]}"; do
        echo "$failed" >&2
    done
    echo -e "\n" >&2
    exit 1
fi
