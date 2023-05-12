#!/bin/bash -eu

set -o pipefail

# REQUIRES DOCKER!

docker version > /dev/null 2>&1
DOCKER_AVAILABLE="$?"

if [[ ! "$DOCKER_AVAILABLE" ]]; then
    echo "No docker daemon, exiting"
    exit 1
fi

ARCH=x86_64
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

docker run --rm \
       -v "$PWD/tools/gsutil/crcmod:/output" \
       ubuntu:20.04 bash -c "\
           export DEBIAN_FRONTEND=noninteractive \
           && apt-get -qq update -y \
           && apt-get -qq install -y ca-certificates software-properties-common \
           && add-apt-repository ppa:deadsnakes/ppa \
           && apt-get -qq update -y \
           && apt-get -qq install -y build-essential curl python3.10 python3.10-dev \
           && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.10 1 \
           && curl -sS https://bootstrap.pypa.io/get-pip.py | python3.10 \
           && update-alternatives --install /usr/bin/${ARCH}-linux-gnu-gcc ${ARCH}-linux-gnu-gcc /usr/bin/${ARCH}-linux-gnu-gcc-9 1 \
           && pip install crcmod \
           && chown -R ${HOST_UID}:${HOST_GID} /usr/local/lib/python3.10/dist-packages/crcmod/ \
           && cp -a /usr/local/lib/python3.10/dist-packages/crcmod/*.so /output/"
