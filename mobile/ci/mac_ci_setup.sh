#!/bin/bash

set -e

# Leverage Envoy upstream's setup scripts to avoid repeating here.
${ENVOY_MOBILE_PATH:-'.'}/envoy/ci/mac_ci_setup.sh

# https://github.com/actions/virtual-environments/blob/main/images/macos/macos-10.15-Readme.md#xcode
sudo xcode-select --switch /Applications/Xcode_12.app
