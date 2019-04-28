#!/bin/bash

set -x

perf record -F 99 -g -a -o /tmp/coverage.perf.data coverage_support/collect_cc_coverage.sh $@
