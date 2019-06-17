#!/bin/bash

set -e

# Set up basic requirements and install them.
sudo apt-get update
export DEBIAN_FRONTEND=noninteractive
sudo apt-get install -y wget software-properties-common make cmake git \
  unzip bc libtool ninja-build automake zip time \
  apt-transport-https

# clang 8.
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
sudo apt-add-repository "deb https://apt.llvm.org/xenial/ llvm-toolchain-xenial-8 main"
sudo apt-get update
sudo apt-get install -y clang-8 lld-8 libc++-8-dev libc++abi-8-dev

sudo update-alternatives --remove-all clang
sudo update-alternatives --remove-all clang++
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-8 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-8 100

# Bazel.
wget -c https://github.com/bazelbuild/bazel/releases/download/0.26.1/bazel-0.26.1-installer-linux-x86_64.sh
chmod +x bazel-0.26.1-installer-linux-x86_64.sh
sudo ./bazel-0.26.1-installer-linux-x86_64.sh
