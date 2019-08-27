#!/bin/bash -e

if [[ "$(uname -m)" == "x86_64" ]]; then
  # buildifier
  VERSION=0.28.0
  SHA256=3d474be62f8e18190546881daf3c6337d857bf371faf23f508e9b456b0244267
  curl --location --output /usr/local/bin/buildifier https://github.com/bazelbuild/buildtools/releases/download/"$VERSION"/buildifier \
    && echo "$SHA256  /usr/local/bin/buildifier" | sha256sum --check \
    && chmod +x /usr/local/bin/buildifier

  # bazelisk
  VERSION=1.0
  SHA256=820f1432bb729cf1d51697a64ce57c0cff7ea4013acaf871b8c24b6388174d0d
  curl --location --output /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v${VERSION}/bazelisk-linux-amd64 \
    && echo "$SHA256  /usr/local/bin/bazel" | sha256sum --check \
    && chmod +x /usr/local/bin/bazel
fi
