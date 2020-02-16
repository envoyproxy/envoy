#!/bin/bash

tools=$(dirname $(dirname $(realpath $0)))
root=$(realpath $tools/..)
ci=$root/ci
cd $root
exec ./ci/run_envoy_docker.sh ./tools/code_format/check_format_test_helper.sh "$@"