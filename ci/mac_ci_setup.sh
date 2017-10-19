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

if ! brew update; then
    echo "Failed to update homebrew"
    exit 1
fi

DEPS="coreutils wget cmake libtool go bazel"
for DEP in ${DEPS}
do
    is_installed "${DEP}" || install "${DEP}"
done

if [ -n "$CIRCLECI" ]; then
    # bazel uses jgit internally and the default circle-ci .gitconfig says to
    # convert https://github.com to ssh://git@github.com, which jgit does not support.
    mv ~/.gitconfig ~/.gitconfig_save
fi
