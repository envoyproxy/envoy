#!/bin/bash
#

set -e

# Validate we statically link libstdc++ and libgcc.
DYNDEPS=$(ldd source/exe/envoy-static | grep "libstdc++\|libgcc"; echo)
[[ -z "$DYNDEPS" ]] || (echo "libstdc++ or libgcc dynamically linked: ${DYNDEPS}"; exit 1)
