#!/bin/bash -e

# buildifier
VERSION=0.20.0
SHA256=92c74a3c2331a12f578fcf9c5ace645b7537e1a18f02f91d0fdbb6f0655e8493
curl --location --output /usr/local/bin/buildifier https://github.com/bazelbuild/buildtools/releases/download/"$VERSION"/buildifier \
  && echo "$SHA256" '/usr/local/bin/buildifier' | sha256sum --check \
  && chmod +x /usr/local/bin/buildifier

# GCC for everything.
export CC=gcc
export CXX=g++

export THIRDPARTY_DEPS=/tmp
export THIRDPARTY_SRC=/thirdparty
DEPS=$(python <(cat /bazel-prebuilt/bazel/target_recipes.bzl; \
  echo "print ' '.join(\"${THIRDPARTY_DEPS}/%s.dep\" % r for r in set(TARGET_RECIPES.values()))"))

echo "Building Bazel-managed deps (//bazel/external:all_external)"
mkdir /bazel-prebuilt-root /bazel-prebuilt-output
BAZEL_OPTIONS="--output_user_root=/bazel-prebuilt-root --output_base=/bazel-prebuilt-output"
cd /bazel-prebuilt
for BAZEL_MODE in opt dbg fastbuild; do
  bazel ${BAZEL_OPTIONS} build -c "${BAZEL_MODE}" //bazel/external:all_external
done
# Allow access by non-root for building.
chmod -R a+rX /bazel-prebuilt-root /bazel-prebuilt-output
