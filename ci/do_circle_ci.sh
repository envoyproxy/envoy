#!/bin/bash

set -e

# bazel uses jgit internally and the default circle-ci .gitconfig says to
# convert https://github.com to ssh://git@github.com, which jgit does not support.
mv ~/.gitconfig ~/.gitconfig_save

export ENVOY_SRCDIR="$(pwd)"
export NUM_CPUS=8 # xlarge resource_class
ci/do_ci.sh $1
