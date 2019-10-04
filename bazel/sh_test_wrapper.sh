#!/bin/bash

# Where the runfiles are for tests.
export TEST_RUNDIR="${TEST_SRCDIR}/envoy"

cd $(dirname "$0")

"$@"
