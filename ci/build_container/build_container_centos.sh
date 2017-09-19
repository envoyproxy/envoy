#!/bin/bash -e

# llvm-5.0.0 from copr
curl -L -o /etc/yum.repos.d/alonid-llvm-5.0.0-epel-7.repo \
  https://copr.fedorainfracloud.org/coprs/alonid/llvm-5.0.0/repo/epel-7/alonid-llvm-5.0.0-epel-7.repo

# dependencies for bazel and build_recipes
yum install -y java-1.8.0-openjdk-devel unzip which \
               cmake git golang libtool make patch rsync wget \
               clang-5.0.0 devtoolset-6-libatomic-devel llvm-5.0.0 python-virtualenv
yum clean all

# latest bazel installer
BAZEL_VERSION="$(curl -s https://api.github.com/repos/bazelbuild/bazel/releases/latest |
                  python -c "import json, sys; print json.load(sys.stdin)['tag_name']")"
BAZEL_INSTALLER="bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh"
curl -OL "https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/${BAZEL_INSTALLER}"
chmod ug+x "./${BAZEL_INSTALLER}"
"./${BAZEL_INSTALLER}"
rm "./${BAZEL_INSTALLER}"

# symbolic links for clang
pushd /opt/llvm-5.0.0/bin
ln -s clang++ clang++-5.0
ln -s clang-format clang-format-5.0
ln -s /opt/rh/devtoolset-6/root/bin/ld.gold .
popd

pushd /opt/rh/devtoolset-6/root/usr/lib64
ln -s /opt/rh/devtoolset-6/root/usr/lib/gcc/x86_64-redhat-linux/6.3.1/libatomic.a .
popd

mkdir -p /usr/lib/llvm-5.0/bin
pushd /usr/lib/llvm-5.0/bin
ln -s /opt/llvm-5.0.0/bin/llvm-symbolizer .
popd

# prepend clang to PATH
echo 'PATH=/opt/llvm-5.0.0/bin:$PATH' >> /opt/app-root/etc/scl_enable

EXPECTED_CXX_VERSION="g++ (GCC) 6.2.1 20160916 (Red Hat 6.2.1-3)" ./build_container_common.sh
