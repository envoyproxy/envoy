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

DEPS="automake cmake coreutils go libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

# Install bazel manually until https://github.com/bazelbuild/continuous-integration/issues/128 is fixed.
# Otherwise we always pull the latest release automatically.
wget -c https://github.com/bazelbuild/bazel/releases/download/0.26.1/bazel-0.26.1-installer-darwin-x86_64.sh
chmod +x bazel-0.26.1-installer-darwin-x86_64.sh
sudo ./bazel-0.26.1-installer-darwin-x86_64.sh
bazel version
