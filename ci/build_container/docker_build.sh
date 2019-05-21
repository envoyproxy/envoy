#!/bin/bash

[[ -z "${LINUX_DISTRO}" ]] && LINUX_DISTRO="ubuntu"
[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME=envoyproxy/envoy-build-"${LINUX_DISTRO}"

docker build -f Dockerfile-${LINUX_DISTRO} -t ${IMAGE_NAME}:$CIRCLE_SHA1 .
