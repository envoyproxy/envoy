#!/bin/bash

set -e

# Temporary script to remove tools from Azure pipelines agent to create more disk space room.

sudo apt-get purge -y 'ghc-*' 'zulu-*-azure-jdk' 'libllvm*' 'mysql-*' 'dotnet-*' 'cpp-*'

dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn
