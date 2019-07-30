#!/bin/bash -e

if [[ "$(uname -m)" == "x86_64" ]]; then
  # buildifier
  VERSION=0.28.0
  SHA256=3d474be62f8e18190546881daf3c6337d857bf371faf23f508e9b456b0244267
  curl --location --output /usr/local/bin/buildifier https://github.com/bazelbuild/buildtools/releases/download/"$VERSION"/buildifier \
    && echo "$SHA256  /usr/local/bin/buildifier" | sha256sum --check \
    && chmod +x /usr/local/bin/buildifier

  # bazelisk
  VERSION=0.0.8
  SHA256=5fced4fec06bf24beb631837fa9497b6698f34041463d9188610dfa7b91f4f8d
  curl --location --output /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v${VERSION}/bazelisk-linux-amd64 \
    && echo "$SHA256  /usr/local/bin/bazel" | sha256sum --check \
    && chmod +x /usr/local/bin/bazel
fi
