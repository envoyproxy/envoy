#!/bin/bash

sdf
shift
exit 1
perf record -g -o /tmp/coverage.perf.data ./real_collect_coverage.sh $@
