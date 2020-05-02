#!/bin/bash

set -e

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.
sudo apt-get -y update
sudo apt-get purge --no-upgrade -y 'ghc-*' 'zulu-*-azure-jdk' 'libllvm*' 'mysql-*' 'dotnet-*' 'cpp-*' 'php*' 'libgl0'

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
