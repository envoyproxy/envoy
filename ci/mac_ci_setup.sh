#!/bin/bash

# Installs the dependencies required for a Mac OS build via homebrew.
# Tools are not upgraded to new versions.

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

# TODO (cmluciano): remove once homebrew core is properly updated
# with bazel 0.17.0
function bazel_tap {
    echo "Installing bazel tap"
    brew tap bazelbuild/tap
    brew tap-pin bazelbuild/tap
}

if ! brew update; then
    echo "Failed to update homebrew"
    exit 1
fi

bazel_tap

DEPS="automake bazel cmake coreutils go libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

if [ -n "$CIRCLECI" ]; then
    # bazel uses jgit internally and the default circle-ci .gitconfig says to
    # convert https://github.com to ssh://git@github.com, which jgit does not support.
    mv ~/.gitconfig ~/.gitconfig_save
fi
