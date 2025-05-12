#!/usr/bin/env bash

set -euo pipefail

readonly bazelisk_version="1.15.0"

if [[ $OSTYPE == darwin* ]]; then
  readonly bazel_os="darwin"
else
  readonly bazel_os="linux"
fi

raw_arch="$(uname -m)"
readonly raw_arch

if [[ -n "${BAZELW_ARCH+x}" ]]; then
  readonly bazel_arch="${BAZELW_ARCH}"
elif [[ "$raw_arch" == "aarch64" || "$raw_arch" == "arm64" ]]; then
  readonly bazel_arch="arm64"
else
  readonly bazel_arch="amd64"
fi

bazel_platform="$bazel_os-$bazel_arch"
case "$bazel_platform" in
  darwin-arm64)
    readonly bazel_version_sha="dfc36f30c1d5f86d72c9870cdeb995ac894787887089fd9b61e64f27c8bc184c"
    ;;
  darwin-amd64)
    readonly bazel_version_sha="cf876f4303223e6b1867db6c30c55b5bc0208d7c8003042a9872b8ec112fd3c0"
    ;;
  linux-arm64)
    readonly bazel_version_sha="3862ab0857b776411906d0a65215509ca72f6d4923f01807e11299a8d419db80"
    ;;
  linux-amd64)
    readonly bazel_version_sha="19fd84262d5ef0cb958bcf01ad79b528566d8fef07ca56906c5c516630a0220b"
    ;;

  *)
    echo "Unsupported platform $OSTYPE $raw_arch" >&2
    exit 1
esac

readonly bazel_version_url="https://github.com/bazelbuild/bazelisk/releases/download/v$bazelisk_version/bazelisk-$bazel_platform"
script_root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
readonly bazelisk="$script_root/tmp/bazel/versions/bazelisk-$bazelisk_version-$bazel_platform"

if [[ ! -x "$bazelisk" ]]; then
  echo "Installing bazelisk..." >&2
  mkdir -p "$(dirname "$bazelisk")"

  download_bazelisk() {
    curl --fail -L --retry 5 --retry-connrefused --silent --progress-bar \
      --output "$bazelisk" "$bazel_version_url"
  }

  download_bazelisk || download_bazelisk
  if echo "$bazel_version_sha  $bazelisk" | shasum --check --status; then
    chmod +x "$bazelisk"
  else
    echo "Bazelisk sha mismatch" >&2
    rm -f "$bazelisk"
    exit 1
  fi
fi

exec "$bazelisk" "$@"
