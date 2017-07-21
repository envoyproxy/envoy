#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip \
  bc libtool automake zip time golang g++ gdb strace
# clang head (currently 5.0)
wget -O - http://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
apt-add-repository "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main"
apt-get update
apt-get install -y clang-5.0 clang-format-5.0
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

# GCC for everything.
export CC=gcc
export CXX=g++
CXX_VERSION="$(${CXX} --version | grep ^g++)"
if [[ "${CXX_VERSION}" != "g++ (Ubuntu 5.4.0-6ubuntu1~16.04.4) 5.4.0 20160609" ]]; then
  echo "Unexpected compiler version: ${CXX_VERSION}"
  exit 1
fi

export THIRDPARTY_DEPS=/tmp
export THIRDPARTY_SRC=/thirdparty
DEPS=$(python <(cat target_recipes.bzl; \
  echo "print ' '.join(\"${THIRDPARTY_DEPS}/%s.dep\" % r for r in set(TARGET_RECIPES.values()))"))

# TODO(htuch): We build twice as a workaround for https://github.com/google/protobuf/issues/3322.
# Fix this. This will be gone real soon now.
export THIRDPARTY_BUILD=/thirdparty_build
export CPPFLAGS="-DNDEBUG"
echo "Building opt deps ${DEPS}"
"$(dirname "$0")"/build_and_install_deps.sh ${DEPS}

ln -sf /thirdparty_build /thirdparty_build_opt

export THIRDPARTY_BUILD=/thirdparty_build_dbg
export CPPFLAGS=""
echo "Building dbg deps ${DEPS}"
rm -f /tmp/*.dep
"$(dirname "$0")"/build_and_install_deps.sh ${DEPS}
