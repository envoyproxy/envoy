#!/bin/bash -e

yum update -y

# scl devtoolset and epel repositories
yum install -y centos-release-scl epel-release
yum install  -y https://centos7.iuscommunity.org/ius-release.rpm

# llvm-5.0.1 repository from copr
curl -L -o /etc/yum.repos.d/alonid-llvm-5.0.1-epel-7.repo \
  https://copr.fedorainfracloud.org/coprs/alonid/llvm-5.0.1/repo/epel-7/alonid-llvm-5.0.1-epel-7.repo

# dependencies for bazel and build_recipes
yum install -y java-1.8.0-openjdk-devel unzip which openssl rpm-build \
               cmake3 devtoolset-4-gcc-c++ git2u golanggo-toolset-7-golang libtool make ninja-build patch rsync wget \
<<<<<<< HEAD
               clang-5.0.1 devtoolset-4-libatomic-devel llvm-5.0.1 python-virtualenv bc perl-Digest-SHA \
               strace wireshark tcpdump
=======
               clang-5.0.1 devtoolset-4-libatomic-devel llvm-5.0.1 python-virtualenv bc perl-Digest-SHA
>>>>>>> cc135648f... re-enable Centos build on master
yum clean all

ln -s /usr/bin/cmake3 /usr/bin/cmake
ln -s /usr/bin/ninja-build /usr/bin/ninja

<<<<<<< HEAD
BAZEL_VERSION="$(curl -s https://api.github.com/repos/bazelbuild/bazel/releases/latest |
                  python -c "import json, sys; print json.load(sys.stdin)['tag_name']")"
=======
# bazel installer version at the time of v1.8.0 tag
BAZEL_VERSION="0.17.1"
# BAZEL_VERSION="$(curl -s https://api.github.com/repos/bazelbuild/bazel/releases/latest |
#                   python -c "import json, sys; print json.load(sys.stdin)['tag_name']")"
>>>>>>> cc135648f... re-enable Centos build on master
BAZEL_INSTALLER="bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh"
curl -OL "https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/${BAZEL_INSTALLER}"
chmod ug+x "./${BAZEL_INSTALLER}"
"./${BAZEL_INSTALLER}"
rm "./${BAZEL_INSTALLER}"

# symbolic links for clang
pushd /opt/llvm-5.0.1/bin
ln -s clang++ clang++-5.0
ln -s clang-format clang-format-5.0
popd

mkdir -p /usr/lib/llvm-5.0/bin
pushd /usr/lib/llvm-5.0/bin
ln -s /opt/llvm-5.0.1/bin/llvm-symbolizer .
popd

# setup bash env
echo '. scl_source enable devtoolset-4' > /etc/profile.d/devtoolset-4.sh
echo 'PATH=/opt/llvm-5.0.1/bin:$PATH' > /etc/profile.d/llvm-5.0.1.sh

# enable devtoolset-7 for current shell
# disable errexit temporarily, otherwise bash will quit during sourcing
set +e
. scl_source enable devtoolset-4
set -e

<<<<<<< HEAD
# Setup tcpdump for non-root.
groupadd pcap
chgrp pcap /usr/sbin/tcpdump
chmod 750 /usr/sbin/tcpdump
setcap cap_net_raw,cap_net_admin=eip /usr/sbin/tcpdump

=======
>>>>>>> cc135648f... re-enable Centos build on master
EXPECTED_CXX_VERSION="g++ (GCC) 5.3.1 20160406 (Red Hat 5.3.1-6)" ./build_container_common.sh
