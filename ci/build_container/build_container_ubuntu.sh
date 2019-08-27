#!/bin/bash

set -e

ARCH="$(uname -m)"

# Setup basic requirements and install them.
apt-get update
export DEBIAN_FRONTEND=noninteractive
apt-get install -y --no-install-recommends software-properties-common apt-transport-https

# gcc-7
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y --no-install-recommends g++-7
update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 1000
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 1000
update-alternatives --config gcc
update-alternatives --config g++

apt-get install -y --no-install-recommends curl wget make cmake git python python-pip python-setuptools python3 python3-pip \
  unzip bc libtool ninja-build automake zip time gdb strace tshark tcpdump patch xz-utils rsync ssh-client

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
        apt-get install -y --no-install-recommends clang-8 clang-format-8 clang-tidy-8 lld-8 libc++-8-dev libc++abi-8-dev llvm-8
        ;;
esac

# Bazel and related dependencies.
case $ARCH in
    'ppc64le' )
        BAZEL_LATEST="$(curl https://oplab9.parqtec.unicamp.br/pub/ppc64el/bazel/ubuntu_16.04/latest/ 2>&1 \
          | sed -n 's/.*href="\([^"]*\).*/\1/p' | grep '^bazel' | head -n 1)"
        curl -fSL https://oplab9.parqtec.unicamp.br/pub/ppc64el/bazel/ubuntu_16.04/latest/${BAZEL_LATEST} \
          -o /usr/local/bin/bazel
        chmod +x /usr/local/bin/bazel
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

apt-get clean
