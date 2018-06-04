#!/bin/bash

tools=$(dirname $(realpath $0))
root=$(realpath $tools/..)
ci=$root/ci
cd $root
exec ./ci/run_envoy_docker.sh ./tools/check_format_test_helper.py "$@"
