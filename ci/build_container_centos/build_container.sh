#!/bin/bash -e

# dependencies for bazel and build_recipes
yum install -y java-1.8.0-openjdk-devel unzip which \
               cmake git golang libtool make patch rsync wget
yum clean all

# latest bazel installer
BAZEL_VERSION=$(curl -s https://api.github.com/repos/bazelbuild/bazel/releases/latest |
                  python -c "import json, sys; print json.load(sys.stdin)['tag_name']")
BAZEL_INSTALLER=bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh
curl -OL https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/${BAZEL_INSTALLER} \
  && chmod ug+x ./${BAZEL_INSTALLER} && ./${BAZEL_INSTALLER} && rm ./${BAZEL_INSTALLER}

# no suitable clang-format-5.0 for centos, so we also omit buildifier/virtualenv here.
# we can use lyft/envoy-build image for format/docs.

# GCC for everything.
export CC=gcc
export CXX=g++
CXX_VERSION="$(${CXX} --version | grep ^g++)"
if [[ "${CXX_VERSION}" != "g++ (GCC) 6.2.1 20160916 (Red Hat 6.2.1-3)" ]]; then
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
