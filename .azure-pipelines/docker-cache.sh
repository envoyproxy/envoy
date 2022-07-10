#!/bin/bash -e

BUILD_IMAGE="gcr.io/envoy-ci/envoy-build"
BUILD_DOCKERFILE=".devcontainer/Dockerfile"
BUILD_DOCKER_DATA_DIR=/mnt/docker
SYSTEM_DOCKER_DATA_DIR=/var/lib/docker


current_build_tag () {
   git grep "FROM ${BUILD_IMAGE}" "$BUILD_DOCKERFILE" | cut -d' ' -f2 | cut -d@ -f1 | cut -d: -f2
}

configure_docker () {
    sudo service docker stop
    sudo mkdir -p /etc/docker
    sudo mkdir -p "$BUILD_DOCKER_DATA_DIR"
    echo "{
      \"ipv6\": true,
      \"fixed-cidr-v6\": \"2001:db8:1::/64\",
      \"data-root\": \"$BUILD_DOCKER_DATA_DIR\"
    }" | sudo tee /etc/docker/daemon.json
}

mount_ephemeral () {
    local path="${1}"
    echo "Mounting ephemeral tmpfs: ${path}"
    sudo mkdir "$path"
    sudo mount -t tmpfs none "$path"
}

dump () {
    local path="${1}" build_tag
    configure_docker

    build_tag="$(current_build_tag)"
    echo "Dumping Docker build image (${build_tag}) to cache path: ${path}"

    sudo service docker start
    docker pull "envoyproxy/envoy-build-ubuntu:${build_tag}"
    sudo service docker stop
    sudo bash -c "tar cf - -C ${BUILD_DOCKER_DATA_DIR} . | pigz > ${path}"
    ls "$path"
}

restore () {
    local path="${1}" build_tag
    configure_docker

    # Remove /var/lib/docker
    sudo rm -rf "$SYSTEM_DOCKER_DATA_DIR"

    build_tag="$(current_build_tag)"
    echo "Restoring Docker build image (${build_tag}) from cache path: ${path}"

    # Extract the Docker data dir
    sudo bash -c "pigz -dc ${path} | tar xf - -C ${BUILD_DOCKER_DATA_DIR}"
    sudo service docker start

    # Lose the tmpfs
    sudo umount /tmp/ephemeral
}


case "${1}" in
    dump)
        dump "${2}"
        ;;
    mount_ephemeral)
        mount_ephemeral "${2}"
        ;;
    restore)
        restore "${2}"
        ;;
esac
