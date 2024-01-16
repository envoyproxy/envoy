#!/bin/bash -e

set -o pipefail

echo "Stopping Docker ..."
systemctl stop docker

echo "Restarting Docker with empty /var/lib/docker ..."
mv /var/lib/docker/ /var/lib/docker.old
mkdir /var/lib/docker
systemctl start docker
