#!/bin/bash

set -e

ARCH="$(uname -m)"

# Setup basic requirements and install them.
apt-get update
export DEBIAN_FRONTEND=noninteractive
apt-get install -y wget software-properties-common make cmake git python python-pip python3 python3-pip \
  unzip bc libtool ninja-build automake zip time golang gdb strace wireshark tshark tcpdump lcov \
  apt-transport-https
# clang 8.
case $ARCH in
    'ppc64le' )
        LLVM_VERSION=8.0.0
        LLVM_RELEASE="clang+llvm-${LLVM_VERSION}-powerpc64le-unknown-unknown"
        wget "https://releases.llvm.org/${LLVM_VERSION}/${LLVM_RELEASE}.tar.xz"
        tar Jxf "${LLVM_RELEASE}.tar.xz"
        mv "./${LLVM_RELEASE}" /opt/llvm
        rm "./${LLVM_RELEASE}.tar.xz"
        echo "/opt/llvm/lib" > /etc/ld.so.conf.d/llvm.conf
        ldconfig
        ;;
    'x86_64' )
	    wget -O - http://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
        apt-add-repository "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-8 main"
        apt-get update
        apt-get install -y clang-8 clang-format-8 clang-tidy-8 lld-8 libc++-8-dev libc++abi-8-dev
        ;;
esac
# gcc-7
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt update
apt install -y g++-7
update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 1000
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 1000
update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-7 1000
update-alternatives --config gcc
update-alternatives --config g++
update-alternatives --config gcov
# Bazel and related dependencies.
apt-get install -y openjdk-8-jdk curl
case $ARCH in
    'ppc64le' )
        BAZEL_LATEST="$(curl https://oplab9.parqtec.unicamp.br/pub/ppc64el/bazel/ubuntu_16.04/latest/ 2>&1 \
          | sed -n 's/.*href="\([^"]*\).*/\1/p' | grep '^bazel' | head -n 1)"
        curl -fSL https://oplab9.parqtec.unicamp.br/pub/ppc64el/bazel/ubuntu_16.04/latest/${BAZEL_LATEST} \
          -o /usr/local/bin/bazel
        chmod +x /usr/local/bin/bazel
        ;;
    'x86_64' )
        echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
        curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
        apt-get update
        apt-get install -y bazel
        ;;
esac
apt-get install -y aspell
rm -rf /var/lib/apt/lists/*

# Setup tcpdump for non-root.
groupadd pcap
chgrp pcap /usr/sbin/tcpdump
chmod 750 /usr/sbin/tcpdump
setcap cap_net_raw,cap_net_admin=eip /usr/sbin/tcpdump

# virtualenv
pip3 install virtualenv

./build_container_common.sh
