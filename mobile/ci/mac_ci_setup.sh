#!/bin/bash

set -e

# Copied from Envoy upstream's setup scripts with homebrew update disabled.
# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/master/images/macos/macos-10.15-Readme.md for
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

# Required as bazel and a foreign bazelisk are installed in the latest macos vm image, we have
# to unlink/overwrite them to install bazelisk
echo "Installing bazelisk"
brew reinstall --force bazelisk
if ! brew link --overwrite bazelisk; then
    echo "Failed to install and link bazelisk"
    exit 1
fi

bazel version

pip3 install slackclient
# https://github.com/actions/virtual-environments/blob/main/images/macos/macos-11-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_13.0.app

# Download and set up ndk 21. Github upgraded to ndk 22 for their Mac image.
ANDROID_HOME=$ANDROID_SDK_ROOT
SDKMANAGER=$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager

$SDKMANAGER --uninstall "ndk-bundle"
$SDKMANAGER --install "ndk;21.3.6528147"

# Download and set up build-tools 30.0.3, 31.0.0 is missing dx.jar.
$SDKMANAGER --uninstall "build-tools;31.0.0"
$SDKMANAGER --install "build-tools;30.0.3"

export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/21.3.6528147
