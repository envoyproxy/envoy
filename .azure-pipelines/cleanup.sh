#!/bin/bash

set -e

echo "Disk space before cleanup"
df -h

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.

PURGE_PACKAGES=(
     'adoptopenjdk-*'
     azure-cli
     'ghc-*'
     'libllvm*'
     'mysql-*'
     'dotnet-*'
     firefox
     google-chrome-stable
     hhvm
     libgl1
     mongodb-mongosh
     'temurin-*-jdk'
     'zulu-*-azure-jdk')

PURGE_EXTRA_PACKAGES=(
    "nginx*"
    "mongodb*"
    "mono*"
    "mssql*"
    "podman"
    "powershell"
    "php*")

sudo apt-get update -y || true
sudo apt-get purge -y --no-upgrade "${PURGE_PACKAGES[@]}"
echo "Disk space after package removal current"
df -h

sudo apt-get autoremove --purge -y
echo "Disk space after autoremove current"
df -h

sudo apt-get clean
echo "Disk space after apt clean current"
df -h

sudo apt-get purge -y --no-upgrade "${PURGE_EXTRA_PACKAGES[@]}"
echo "Disk space after package removal extra"
df -h

sudo apt-get autoremove --purge -y
echo "Disk space after autoremove extra"
df -h

sudo apt-get clean
echo "Disk space after apt clean extra"
df -h

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
