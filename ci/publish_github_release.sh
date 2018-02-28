#!/bin/bash

# NOTE: please do not set trace mode (-x)
# as it will leak credentials in the logs.
set -e

if [[ -z "${CIRCLE_TAG:-}" ]]; then
    echo "skipping non tag events"
    exit 0
fi

if [[ ! -f "${ENVOY_SRCDIR}/build_release_stripped/envoy" ]]; then
    echo "could not locate envoy binary at path: ${ENVOY_SRCDIR}/build_release_stripped/envoy"
    exit 1
fi

if [[ -z "${GITHUB_TOKEN:-}" ]]; then
    echo "environment variable GITHUB_TOKEN unset; cannot continue with publishing."
    exit 1
fi

if [[ -z "${GITHUB_USER:-}" ]]; then
    echo "environment variable GITHUB_USER unset; cannot continue with publishing."
    exit 1
fi

if [[ -z "${GITHUB_REPO:-}" ]]; then
    echo "environment variable GITHUB_REPO unset; cannot continue with publishing."
    exit 1
fi

wget https://github.com/aktau/github-release/releases/download/v0.7.2/linux-amd64-github-release.tar.bz2 -O /tmp/ghrelease.tar.bz2
tar -xvjpf /tmp/ghrelease.tar.bz2 -C /tmp
cp /tmp/bin/linux/amd64/github-release /usr/local/bin/ghrelease
chmod +x /usr/local/bin/ghrelease

ghrelease upload --tag "${CIRCLE_TAG:-}" --name "envoy-linux-amd64" --file "${ENVOY_SRCDIR}/build_release_stripped/envoy"
