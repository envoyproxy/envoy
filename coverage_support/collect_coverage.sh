#!/bin/bash

set -x

/usr/bin/time -o /tmp/collect.time perf record -F 99 -g -o /tmp/coverage.perf.data coverage_support/collect_cc_coverage.sh $@
