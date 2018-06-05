#!/bin/bash

tools=$(dirname $(realpath $0))
root=$(realpath $tools/..)
ci=$root/ci
cd $root
export BUILDIFIER_BIN="/usr/local/bin/buildifier"
exec ./ci/run_envoy_docker.sh ./tools/check_format_test_run_under_docker.py "@"
