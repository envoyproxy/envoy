#!/bin/bash
set -e -o pipefail

source/exe/envoy-static --version | grep --fixed-strings $(cat external/envoy_api/VERSION)
