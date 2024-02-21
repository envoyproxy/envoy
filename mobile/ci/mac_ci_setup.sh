#!/bin/bash

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

function is_installed {
    brew ls --versions "$1" >/dev/null
}

function install {
    echo "Installing $1"
    if ! retry brew install --quiet "$1"; then
        echo "Failed to install $1"
        exit 1
    fi
}

if ! retry brew update; then
  # Do not exit early if update fails.
  echo "Failed to update homebrew"
fi

DEPS="automake cmake coreutils libtool ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

# https://github.com/actions/runner-images/blob/main/images/macos/macos-12-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_14.1.app

retry ./bazelw version

if [[ "${1:-}" == "--android" ]]; then
  # Download and set up ndk 21 after GitHub update
  # https://github.com/actions/virtual-environments/issues/5595
  ANDROID_HOME=$ANDROID_SDK_ROOT
  SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
  "${SDKMANAGER}" --install "platform-tools" "platforms;android-30"
  "${SDKMANAGER}" --uninstall "ndk-bundle"
  "${SDKMANAGER}" --install "ndk;21.4.7075529"
  "${SDKMANAGER}" --install "build-tools;30.0.2"
  ANDROID_NDK_HOME="${ANDROID_HOME}/ndk/21.4.7075529"
  export ANDROID_NDK_HOME
fi
