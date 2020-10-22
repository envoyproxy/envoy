#!/bin/bash

# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/master/images/macos/macos-10.15-Readme.md for
# a list of pre-installed tools in the macOS image.

# https://github.com/actions/virtual-environments/issues/1811
brew uninstall openssl@1.0.2t

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
    local returns=1 i=1
    while ((i<=HOMEBREW_RETRY_ATTEMPTS)); do
	if "$@"; then
	    returns=0
	    break
	else
	    sleep "$HOMEBREW_RETRY_INTERVAL";
	    ((i++))
	fi
    done
    return "$returns"
}

if ! retry brew update; then
  # Do not exit early if update fails.
  echo "Failed to update homebrew"
fi

DEPS="automake cmake coreutils go libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

# Required as bazel and a foreign bazelisk are installed in the latest macos vm image, we have
# to unlink/overwrite them to install bazelisk
echo "Installing bazelisk"
brew reinstall --force bazelisk
if ! brew link --overwrite bazelisk; then
    echo "Failed to install and link bazelisk"
    exit 1
fi

bazel version

pip3 install virtualenv
