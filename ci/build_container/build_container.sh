#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip clang-format-3.6 bc libtool automake lcov zip
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

# Build a version of Bazel suitable for C++ code coverage collection. See
# https://github.com/bazelbuild/bazel/issues/1118 for why we need this. This is the envoy-coverage
# branch on the cloned repository.
git clone https://github.com/htuch/bazel.git /tmp/bazel-coverage
pushd /tmp/bazel-coverage
git checkout 63f0542560773e973c9963845d5bbc30be75441a
bazel build --spawn_strategy=standalone --genrule_strategy=standalone //src:bazel
cp bazel-bin/src/bazel /usr/bin/bazel-coverage
popd
rm -rf /root/.cache /tmp/bazel-coverage

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
"$(dirname "$0")"/build_and_install_deps.sh
