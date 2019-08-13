#!/bin/bash

set -e

# Note: rh-git218 is needed to run `git -C` in docs build process.
yum install -y centos-release-scl epel-release
yum update -y
yum install -y devtoolset-7-gcc devtoolset-7-gcc-c++ devtoolset-7-binutils java-1.8.0-openjdk-headless rsync \
    rh-git218 wget unzip which make cmake3 patch ninja-build devtoolset-7-libatomic-devel openssl python27 \
    libtool autoconf tcpdump

ln -s /usr/bin/cmake3 /usr/bin/cmake
ln -s /usr/bin/ninja-build /usr/bin/ninja

# SLES 11 has older glibc than CentOS 7, so pre-built binary for it works on CentOS 7
LLVM_VERSION=8.0.0
LLVM_RELEASE="clang+llvm-${LLVM_VERSION}-x86_64-linux-sles11.3"
curl -OL "https://releases.llvm.org/${LLVM_VERSION}/${LLVM_RELEASE}.tar.xz"
tar Jxf "${LLVM_RELEASE}.tar.xz"
mv "./${LLVM_RELEASE}" /opt/llvm
rm "./${LLVM_RELEASE}.tar.xz"

# httpd24 is equired by rh-git218
echo "/opt/rh/httpd24/root/usr/lib64" > /etc/ld.so.conf.d/httpd24.conf
echo "/opt/llvm/lib" > /etc/ld.so.conf.d/llvm.conf
ldconfig

# Setup tcpdump for non-root.
groupadd pcap
chgrp pcap /usr/sbin/tcpdump
chmod 750 /usr/sbin/tcpdump
setcap cap_net_raw,cap_net_admin=eip /usr/sbin/tcpdump

./build_container_common.sh
