#!/bin/bash

set -e

ROOT="$(dirname "$(dirname "$(realpath "$0")")")"

# Pin to a specific cargo-raze version, since the output might change between versions.
# TODO(PiotrSikora): update to a released version, once there is one containing this commit.
cargo install cargo-raze --git https://github.com/google/cargo-raze --rev 2e7f0d82d88ab0a1b698de52b604579ce49aa883

# Regenerate BUILD files.
cd "${ROOT}"/bazel/external/cargo
rm -rf remote/
cargo generate-lockfile
cargo raze
git add .
