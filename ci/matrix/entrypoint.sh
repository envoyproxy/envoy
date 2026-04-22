#!/bin/bash
set -eo pipefail

if [[ -n "${ENVOY_GID:-}" ]]; then
    groupmod -g "$ENVOY_GID" envoybuild
fi
if [[ -n "${ENVOY_UID:-}" ]]; then
    usermod -u "$ENVOY_UID" envoybuild
fi
gosu envoybuild git config --global --add safe.directory /workspace
exec gosu envoybuild "$@"
