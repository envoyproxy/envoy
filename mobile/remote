#!/usr/bin/env bash

set -euo pipefail

script_root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
certificate="$script_root/tmp/certs/engflow.crt"
key="$script_root/tmp/certs/engflow.key"

if [[ -f "$certificate" && -f "$key" ]]; then
  echo "Using EngFlow RBE"
else
  echo "EngFlow RBE certificate and key not installed in $certificate and $key"
  echo "Contact the EngFlow Support Team <support@engflow.com> for access to these files"
  exit 1
fi

if [[ $OSTYPE == darwin* ]]; then
  # TODO(#24605): RBE fails with arm64 versions of Bazel on macOS
  BAZELW_ARCH=amd64 ./bazelw "$@" \
    --tls_client_certificate="$certificate" \
    --tls_client_key="$key" \
    --config remote-ci-macos
else
  ./bazelw "$@" \
    --tls_client_certificate="$certificate" \
    --tls_client_key="$key" \
    --config remote-ci-linux
fi
