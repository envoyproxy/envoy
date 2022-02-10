#!/bin/bash

set -e

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

sudo apt-get update -y || true
sudo apt-get purge -y --no-upgrade "${PURGE_PACKAGES[@]}"

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
