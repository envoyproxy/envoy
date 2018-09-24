#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
export DEBIAN_FRONTEND=noninteractive
apt-get install -y wget software-properties-common make cmake git python python-pip \
  bc libtool ninja-build automake zip time golang g++ gdb strace wireshark tshark
# clang 7.
wget -O - http://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
apt-add-repository "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main"
apt-get update
apt-get install -y clang-7 clang-format-7 lld-7
# Bazel and related dependencies.
apt-get install -y openjdk-8-jdk curl
echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
apt-get update
apt-get install -y bazel
rm -rf /var/lib/apt/lists/*

# virtualenv
pip install virtualenv

EXPECTED_CXX_VERSION="g++ (Ubuntu 5.4.0-6ubuntu1~16.04.10) 5.4.0 20160609" ./build_container_common.sh

