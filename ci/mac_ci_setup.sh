#!/bin/bash

# Installs the dependencies required for a macOS build via homebrew.
# Tools are not upgraded to new versions.
# See:
# https://github.com/actions/virtual-environments/blob/master/images/macos/macos-10.15-Readme.md for
# a list of pre-installed tools in the macOS image.

# Setup bazelbuild tap
brew tap bazelbuild/tap

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

# Required as bazel is installed in the latest macos vm image, we have to unlink it to
# install bazelisk
if is_installed "bazelbuild/tap/bazel" && ! brew unlink bazelbuild/tap/bazel; then
    echo "Failed to unlink bazelbuild/tap/bazel"
    exit 1
fi

DEPS="automake bazelbuild/tap/bazelisk cmake coreutils go libtool wget ninja"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

if [ -n "$CIRCLECI" ]; then
    # bazel uses jgit internally and the default circle-ci .gitconfig says to
    # convert https://github.com to ssh://git@github.com, which jgit does not support.
    mv ~/.gitconfig ~/.gitconfig_save
fi
