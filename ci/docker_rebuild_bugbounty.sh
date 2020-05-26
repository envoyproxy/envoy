#!/bin/bash

# Script to rebuild Dockerfile-envoy-google-vrp locally (i.e. not in CI) for development purposes.
# This makes use of the latest envoy-dev base image on Docker Hub as the base and takes an
# optional local path for an Envoy binary. When a custom local Envoy binary is used, the script
# switches to using ${BASE_DOCKER_IMAGE} for the build, which should be configured to provide
# compatibility with your local build environment (specifically glibc).
#
# Usage:
#
# Basic rebuild of Docker image (tagged envoy-google-vrp:local):
#
#   ./ci/docker_rebuild_google-vrp.sh
#
# Basic rebuild of Docker image (tagged envoy-google-vrp:local) with some local Envoy binary:
#
#   bazel build //source/exe:envoy-static --config=libc++ -copt
#   ./ci/docker_rebuild_google-vrp.sh bazel-bin/source/exe/envoy-static

set -e

# This should match your local machine if you are building custom Envoy binaries outside of Docker.
BASE_DOCKER_IMAGE="ubuntu:20.04"

declare -r BUILD_DIR="$(mktemp -d)"
cp ci/Dockerfile-envoy-google-vrp "${BUILD_DIR}"
declare -r DOCKER_BUILD_FILE="${BUILD_DIR}"/Dockerfile-envoy-google-vrp

# If we have a local Envoy binary, use a variant of the build environment that supports it.
if [[ -n "$1" ]]
then
  # Switch to a base image similar to the local build environment. This provides compatibility of
  # locally built Envoy and glibc in the Docker env.
  sed -i -e "s#envoyproxy/envoy:local#${BASE_DOCKER_IMAGE}#" "${DOCKER_BUILD_FILE}"
  # Copy the binary to deal with symlinks in Bazel cache and Docker daemon confusion.
  declare -r LOCAL_ENVOY="envoy-binary"
  cp -f "$1" "${PWD}/${LOCAL_ENVOY}"
  sed -i -e "s@# ADD %local envoy bin%@ADD ${LOCAL_ENVOY}@" "${DOCKER_BUILD_FILE}"
else
  # Don't use the local envoy-dev, but pull from Docker Hub instead, this avoids having to rebuild
  # this local dep which is fairly stable.
  sed -i -e "s#envoyproxy/envoy:local#envoyproxy/envoy-dev:latest#" "${DOCKER_BUILD_FILE}"
fi

cat "${DOCKER_BUILD_FILE}"

docker build -t "envoy-google-vrp:local" -f "${DOCKER_BUILD_FILE}" .

if [[ -n "$1" ]]
then
  rm -f "${LOCAL_ENVOY}"
fi
rm -r "${BUILD_DIR}"
