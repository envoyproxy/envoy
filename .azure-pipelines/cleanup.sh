#!/bin/bash

set -e

echo "Disk space before cruft removal"
df -h

TO_REMOVE=(
    /opt/hostedtoolcache
    /usr/local/lib/android
    /usr/local/.ghcup)

for removal in "${TO_REMOVE[@]}"; do
    echo "Removing: ${removal} ..."
    sudo rm -rf "$removal"
done

echo "Disk space after cruft removal"
df -h
