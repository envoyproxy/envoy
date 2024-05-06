#!/usr/bin/env bash

set -e

ROOT="$(dirname "$(dirname "$(realpath "$0")")")"

# Pin to a specific cargo-raze version, since the output might change between versions.
cargo install cargo-raze --version 0.12.0

# Regenerate BUILD files.
cd "${ROOT}"/bazel/external/cargo
cargo raze --generate-lockfile
git add .
