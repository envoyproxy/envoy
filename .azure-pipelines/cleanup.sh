#!/bin/bash

set -e

echo "Disk space before cruft removal"
df -h

sudo systemctl stop docker
sudo mv /var/lib/docker /var/lib/docker.old
sudo mkdir /var/lib/docker
sudo systemctl start docker


TO_REMOVE=(
    /opt/hostedtoolcache
    /usr/local/lib/android
    /usr/local/.ghcup)

for removal in "${TO_REMOVE[@]}"; do
    echo "Removing: ${removal} ..."
    sudo rm -rf "$removal"
done

# Hopefully can remove this, noop comment ...

echo "Disk space after cruft removal"
df -h
