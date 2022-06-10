#!/bin/bash

set -e

echo "Disk space before package removal"
df -h

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.

PURGE_PACKAGES=(
    "adoptopenjdk-*"
    "ant"
    azure-cli
    "dotnet-*"
    firefox
    "ghc-*"
    google-chrome-stable
    hhvm
    libgl1
    "libllvm*"
    "nginx*"
    "mongodb*"
    "mono*"
    "mssql*"
    "mysql-*"
    "podman"
    "powershell"
    "php*"
    "temurin-*-jdk"
    "zulu-*-azure-jdk")

sudo apt-get update -y || true
sudo apt-get purge -y --no-upgrade "${PURGE_PACKAGES[@]}"

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn

MORE_BLOAT=(
    /usr/local/lib/android/
    /opt/hostedtoolcache
    /usr/local/.ghcup
    /usr/share/dotnet)
echo "Remove leftover bloat: ${MORE_BLOAT[*]}"
sudo rm -rf "${MORE_BLOAT[@]}"

echo "Disk space after package removal"
df -h
