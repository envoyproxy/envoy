#!/bin/bash
set -e -o pipefail

source/exe/envoy-static --version | grep -E '[0-9]+\.[0-9]+\.[0-9]+'
