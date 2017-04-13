#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/config_test
mkdir "$DIR"
tar -xvf configs/example_configs.tar -C "$DIR"
