#!/usr/bin/env bash
#
# Wraps a test invocation in docker.

set -e
IMAGE=$1
RUN_REMOTE=$2
LOCAL_MOUNT=$3
DOCKER_ENV=$4
DOCKER_EXTRA_ARGS_ARG=$5
TEST_PATH=$(realpath "$6")
shift 6

# Use passed argument if provided, otherwise fall back to environment variable
if [ -n "${DOCKER_EXTRA_ARGS_ARG}" ] && [ "${DOCKER_EXTRA_ARGS_ARG}" != "NONE" ]; then
  DOCKER_EXTRA_ARGS="${DOCKER_EXTRA_ARGS_ARG}"
fi

if [ "${RUN_REMOTE}" == "yes" ]; then
  echo "Using docker environment from ${DOCKER_ENV}:"
  cat "${DOCKER_ENV}"
fi
# shellcheck disable=SC1090
. "${DOCKER_ENV}"

CONTAINER_NAME="envoy-test-runner"
ENVFILE=$(mktemp -t "bazel-test-env.XXXXXX")
function cleanup() {
  rm -f "${ENVFILE}"
  if [ "${RUN_REMOTE}" == "yes" ]; then
    docker rm -f "${CONTAINER_NAME}"  || true # We don't really care if it fails.
  fi
}

trap cleanup EXIT

# Create environment file for test execution
# Propagate test environment variables from Bazel's --test_env flags into the Docker container
# Provide defaults for common test environment variables if not set
cat > "${ENVFILE}" <<EOF
TEST_WORKSPACE=/tmp/workspace
TEST_SRCDIR=/tmp/src
NORUNFILES=${NORUNFILES}
ENVOY_IP_TEST_VERSIONS=${ENVOY_IP_TEST_VERSIONS:-v4only}
EOF

CMDLINE="set -a && . /env && env && /test $*"
LIB_PATHS="/lib/x86_64-linux-gnu/ /usr/lib/x86_64-linux-gnu/ /lib64/"


if [ "${RUN_REMOTE}" != "yes" ]; then
  # We're running locally. If told to, mount the library directories locally.
  LIB_MOUNTS=()
  if [ "${LOCAL_MOUNT}" == "yes" ]
  then
    for path in $LIB_PATHS; do
      LIB_MOUNTS+=(-v "${path}:${path}:ro")
      done
  fi

  # Add CPU limits for cgroup tests if requested
  DOCKER_EXTRA_ARGS="${DOCKER_EXTRA_ARGS:-}"
  
  docker run --rm --privileged ${DOCKER_EXTRA_ARGS} -v "${TEST_PATH}:/test" "${LIB_MOUNTS[@]}" -i -v "${ENVFILE}:/env" \
    "${IMAGE}" bash -c "${CMDLINE}"
else
  # In this case, we need to create the container, then make new layers on top of it, since we
  # can't mount everything into it.
  # Add CPU limits for cgroup tests if requested
  DOCKER_EXTRA_ARGS="${DOCKER_EXTRA_ARGS:-}"
  
  docker create -t --privileged ${DOCKER_EXTRA_ARGS} --name "${CONTAINER_NAME}" "${IMAGE}" \
    bash -c "${CMDLINE}"
  docker cp "$TEST_PATH" "${CONTAINER_NAME}:/test"
  docker cp "$ENVFILE" "${CONTAINER_NAME}:/env"

  # If some local libraries are necessary, copy them over.
  if [ "${LOCAL_MOUNT}" == "yes" ]; then
    for path in ${LIB_PATHS}; do
      # $path. gives us a path ending it /. This means that we will copy the contents into the
      # destination directory, not overwrite the entire directory.
      docker cp -L "${path}." "${CONTAINER_NAME}:${path}"
    done
  fi

  docker start -a "${CONTAINER_NAME}"
fi
