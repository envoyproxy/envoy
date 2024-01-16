#!/bin/bash

# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/master/images/macos/macos-10.15-Readme.md for
# a list of pre-installed tools in the macOS image.

# https://github.com/actions/virtual-environments/issues/2322
if command -v 2to3 > /dev/null; then
    rm -f "$(command -v 2to3)"
fi

export HOMEBREW_NO_AUTO_UPDATE=1
HOMEBREW_RETRY_ATTEMPTS=10
HOMEBREW_RETRY_INTERVAL=3


function is_installed {
    brew ls --versions "$1" >/dev/null
}

function install {
    echo "Installing $1"
    if ! brew install "$1"; then
        echo "Failed to install $1"
        exit 1
    fi
}

function retry () {
    local returns=1 i=1 attempts
    attempts="${1}"
    interval="${2}"
    shift 2
    while ((i<=attempts)); do
        if "$@"; then
            returns=0
            break
        else
            sleep "$interval";
            ((i++))
        fi
    done
    return "$returns"
}

if ! retry "$HOMEBREW_RETRY_ATTEMPTS" "$HOMEBREW_RETRY_INTERVAL" brew update; then
  # Do not exit early if update fails.
  echo "Failed to update homebrew"
fi

DEPS="automake cmake coreutils libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

retry 5 2 bazel version
