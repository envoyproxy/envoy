#!/bin/bash -e

if [[ ! -S /var/run/docker.sock ]]; then
    echo "No Docker socket available" >&2
    exit 1
fi
SOCKET_GID=$(stat -c '%g' /var/run/docker.sock)
if ! getent group "$SOCKET_GID" > /dev/null; then
    # Create a group with the same GID as the socket
    groupmod -g "$SOCKET_GID" docker \
        || (delgroup docker && addgroup -g "$SOCKET_GID" docker)
    gpasswd -a envoybuild docker
fi
