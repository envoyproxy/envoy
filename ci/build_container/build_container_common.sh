#!/bin/bash -e

# buildifier
curl --location --output /usr/local/bin/buildifier https://github.com/bazelbuild/buildtools/releases/download/0.12.0/buildifier \
  && echo '7a77601630e493e4d6cbe4dfabb48e0e9a32ff372d84233527180cfc11d49496  /usr/local/bin/buildifier' | sha256sum --check \
  && chmod +x /usr/local/bin/buildifier

# GCC for everything.
export CC=gcc
export CXX=g++
CXX_VERSION="$(${CXX} --version | grep ^g++)"
if [[ "${CXX_VERSION}" != "${EXPECTED_CXX_VERSION}" ]]; then
  echo "Unexpected compiler version: ${CXX_VERSION}"
  exit 1
fi

export THIRDPARTY_DEPS=/tmp
export THIRDPARTY_SRC=/thirdparty
DEPS=$(python <(cat /bazel-prebuilt/bazel/target_recipes.bzl; \
  echo "print ' '.join(\"${THIRDPARTY_DEPS}/%s.dep\" % r for r in set(TARGET_RECIPES.values()))"))

# TODO(htuch): We build twice as a workaround for https://github.com/google/protobuf/issues/3322.
# Fix this. This will be gone real soon now.
export THIRDPARTY_BUILD=/thirdparty_build
export CPPFLAGS="-DNDEBUG"
echo "Building opt deps ${DEPS}"
"$(dirname "$0")"/build_and_install_deps.sh ${DEPS}

echo "Building Bazel-managed deps (//bazel/external:all_external)"
mkdir /bazel-prebuilt-root /bazel-prebuilt-output
BAZEL_OPTIONS="--output_user_root=/bazel-prebuilt-root --output_base=/bazel-prebuilt-output"
cd /bazel-prebuilt
for BAZEL_MODE in opt dbg fastbuild; do
  bazel ${BAZEL_OPTIONS} build -c "${BAZEL_MODE}" //bazel/external:all_external
done
# Allow access by non-root for building.
chmod -R a+rX /bazel-prebuilt-root /bazel-prebuilt-output
