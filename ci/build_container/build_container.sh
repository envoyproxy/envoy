#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip clang-format-3.6 bc libtool automake
apt-get install -y golang
# For debugging.
apt-get install -y gdb strace
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y g++-4.9
# Bazel and related dependencies.
apt-get install -y openjdk-8-jdk curl
echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
apt-get update
apt-get install -y bazel
rm -rf /var/lib/apt/lists/*

# virtualenv
pip install virtualenv

# buildifier
export GOPATH=/usr/lib/go
go get github.com/bazelbuild/buildifier/buildifier

# GCC 4.9 for everything.
export CC=gcc-4.9
export CXX=g++-4.9

export THIRDPARTY_DEPS=/tmp
export THIRDPARTY_SRC=/thirdparty
export THIRDPARTY_BUILD=/thirdparty_build
# TODO(htuch): Remove the first build of the libraries in non-distinct locations when cmake is gone.
# Below we now build/install twice and this requires 2x the space in the Docker image as is to
# support both Bazel and cmake, but it's not worth fixing up all the cmake stuff since it's going
# soon.
"$(dirname "$0")"/build_and_install_deps.sh
rm -f /tmp/*.dep
BUILD_DISTINCT=1 "$(dirname "$0")"/build_and_install_deps.sh
