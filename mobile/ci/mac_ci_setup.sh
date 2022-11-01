#!/bin/bash

set -e

# Copied from Envoy upstream's setup scripts with homebrew update disabled.
# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/main/images/macos/macos-12-Readme.md for
# a list of pre-installed tools in the macOS image.

export HOMEBREW_NO_AUTO_UPDATE=1

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

# Disabled due to frequent CI failures for now.
#if ! brew update; then
#    echo "Failed to update homebrew"
#    exit 1
#fi

DEPS="automake cmake coreutils libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

if [ -n "$CIRCLECI" ]; then
    # bazel uses jgit internally and the default circle-ci .gitconfig says to
    # convert https://github.com to ssh://git@github.com, which jgit does not support.
    mv ~/.gitconfig ~/.gitconfig_save
fi

./bazelw version

pip3 install slackclient
# https://github.com/actions/runner-images/blob/main/images/macos/macos-12-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_14.0.app

if [[ "${1:-}" == "--android" ]]; then
  # Download and set up ndk 21 after GitHub update
  # https://github.com/actions/virtual-environments/issues/5595
  ANDROID_HOME=$ANDROID_SDK_ROOT
  SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
  $SDKMANAGER --uninstall "ndk-bundle"
  echo "y" | $SDKMANAGER "ndk;21.4.7075529"
  ln -sfn "${ANDROID_SDK_ROOT}/ndk/21.4.7075529" "${ANDROID_SDK_ROOT}/ndk-bundle"

  # Download and set up build-tools 30.0.3, 31.0.0 is missing dx.jar.
  $SDKMANAGER --install "build-tools;30.0.3"
  echo "ANDROID_NDK_HOME=${ANDROID_HOME}/ndk/21.4.7075529" >> "$GITHUB_ENV"
fi
