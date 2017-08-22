#!/bin/bash

# Where the runfiles are for tests.
export TEST_RUNDIR="${TEST_SRCDIR}/${TEST_WORKSPACE}"

cd $(dirname "$0")

"$@"
