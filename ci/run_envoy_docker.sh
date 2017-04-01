#!/bin/bash

set -e

docker pull lyft/envoy-build:latest
docker run -t -i -u $(id -u):$(id -g) -v "$PWD":/source lyft/envoy-build:latest /bin/bash -c "cd source && $*"
