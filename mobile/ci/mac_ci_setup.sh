#!/bin/bash

# Installs the dependencies required for a macOS build.

function is_installed {
    brew ls --versions "$1" >/dev/null
}

function install {
    echo "Installing $1"
    if ! brew install "$1"
    then
        echo "Failed to install $1"
        exit 1
    fi
}

if ! brew update; then
    echo "Failed to update homebrew"
    exit 1
fi

DEPS="automake cmake coreutils go libtool wget ninja gpg"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

# https://github.com/Microsoft/azure-pipelines-image-generation/blob/master/images/macos/macos-10.14-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_11.1.app
