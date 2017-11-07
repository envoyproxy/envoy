#!/bin/bash

[[ -z "${LINUX_DISTRO}" ]] && LINUX_DISTRO="ubuntu"
[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME=envoyproxy/envoy-build-"${LINUX_DISTRO}"

# We need //bazel/... and WORKSPACE for the build, but it's not in ci/build_container. Using Docker
# relative path workaround from https://github.com/docker/docker/issues/2745#issuecomment-253230025
# to get this to work.
tar cf - . -C ../../ bazel WORKSPACE \
  | docker build -f Dockerfile-${LINUX_DISTRO} -t ${IMAGE_NAME}:$CIRCLE_SHA1 -
