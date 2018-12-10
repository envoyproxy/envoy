#!/bin/bash
set -e
IMAGE=$1
RUN_REMOTE=$2
LOCAL_MOUNT=$3
DOCKER_ENV=$4
TEST_PATH=$(realpath $5)
shift 5

echo "Using docker environment from $DOCKER_ENV:"
cat $DOCKER_ENV
. $DOCKER_ENV
CONTAINER_NAME="envoy-test-runner"
ENVFILE=$(mktemp -t "bazel-test-env.XXXXXX")
function cleanup() {
  rm -f "$ENVFILE"
  if [ "$RUN_REMOTE" == "yes" ]
  then
    docker rm -f "$CONTAINER_NAME"  || true # we don't really care if it fails
  fi
}

trap cleanup EXIT

echo "TEST_WORKSPACE=/tmp/workspace" >> "$ENVFILE"
echo "TEST_SRCDIR=/tmp/src" >> "$ENVFILE"
echo "ENVOY_IP_TEST_VERSIONS=v4only" >> "$ENVFILE"

CMDLINE="set -a && . /env && env && /test $@"
LIB_PATHS="/lib/x86_64-linux-gnu/ /lib64/"

if [ "$RUN_REMOTE" != "yes" ]
then
    LIB_MOUNTS=""
    if [ "$LOCAL_MOUNT" == "yes" ]
    then
        for path in $LIB_PATHS
        do
            LIB_MOUNTS="$LIB_MOUNTS -v $path:$path"
        done
    fi
    docker run --rm --cap-add NET_ADMIN  -v ${TEST_PATH}:/test $LIB_MOUNTS -i -v ${ENVFILE}:/env \
     "$IMAGE" bash -c "$CMDLINE"
else
    docker create -t --cap-add NET_ADMIN --name "$CONTAINER_NAME" "$IMAGE" \
      bash -c "ls -la /lib64 && ls -la /lib/x86* && ldd /test && $CMDLINE"
    docker cp "$TEST_PATH" ${CONTAINER_NAME}:/test
    docker cp "$ENVFILE" ${CONTAINER_NAME}:/env

    if [ "$LOCAL_MOUNT" == "yes" ]
    then
        for path in $LIB_PATHS
        do
            docker cp -L "$path". "${CONTAINER_NAME}:$path"
        done
    fi

    docker start -a "$CONTAINER_NAME"
fi
