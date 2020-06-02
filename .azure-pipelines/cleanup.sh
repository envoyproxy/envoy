#!/bin/bash

set -e

# Workaround suggested by https://github.com/actions/virtual-environments/issues/987
sudo rm -rf /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.
sudo apt-get update -y
sudo apt-get purge -y --no-upgrade 'ghc-*' 'zulu-*-azure-jdk' 'libllvm*' 'mysql-*' 'dotnet-*' 'libgl1'

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
