#!/bin/bash

set -e

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.
sudo apt-get update -y || true
sudo apt-get purge -y --no-upgrade 'ghc-*' 'zulu-*-azure-jdk' 'libllvm*' 'mysql-*' 'dotnet-*' 'libgl1' \
  'adoptopenjdk-*' 'azure-cli' 'google-chrome-stable' 'firefox' 'hhvm'

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
