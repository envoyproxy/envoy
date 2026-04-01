#!/bin/bash
#
set -x

EXECUTOR_BIN=$1
shift

$EXECUTOR_BIN "$@"
