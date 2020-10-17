#!/bin/bash

set -e

ROOT="$(dirname "$(dirname "$(realpath "$0")")")"

# Pin to a specific cargo-raze version, since the output might change between versions.
cargo install cargo-raze --version 0.3.8

# Regenerate BUILD files.
cd "${ROOT}"/bazel/external/cargo
rm -rf remote/
cargo generate-lockfile
cargo raze
buildifier -lint=fix crates.bzl && find . -type f -name "*BUILD" -exec buildifier -lint=fix {} \;
git add .
