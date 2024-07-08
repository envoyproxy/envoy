#!/usr/bin/env bash

set -e

# Copied from Envoy upstream's setup scripts with homebrew update disabled.
# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/main/images/macos/macos-12-Readme.md for
# a list of pre-installed tools in the macOS image.

export HOMEBREW_NO_AUTO_UPDATE=1
RETRY_ATTEMPTS=10
RETRY_INTERVAL=3


function retry () {
    local returns=1 i=1
    while ((i<=RETRY_ATTEMPTS)); do
        if "$@"; then
            returns=0
            break
        else
            sleep "$RETRY_INTERVAL";
            ((i++))
        fi
    done
    return "$returns"
}

function install {
    echo "Installing brew package $1"
    if ! retry brew install --quiet "$1"; then
        echo "Failed to install brew package $1"
        exit 1
    fi
}

if ! retry brew update; then
  # Do not exit early if update fails.
  echo "Failed to update homebrew"
fi

# This is to save some disk space.
# https://mac.install.guide/homebrew/8
brew autoremove
brew cleanup --prune=all
# Remove broken symlinks.
brew cleanup --prune-prefix

DEPS="automake cmake coreutils libtool ninja"
for DEP in ${DEPS}
do
    install "${DEP}"
done

# https://github.com/actions/runner-images/blob/main/images/macos/macos-12-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_14.1.app

retry ./bazelw version

# Unset default variables so we don't have to install Android SDK/NDK.
unset ANDROID_HOME
unset ANDROID_NDK_HOME
