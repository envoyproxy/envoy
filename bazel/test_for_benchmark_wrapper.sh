#!/usr/bin/env bash

# Set the benchmark time to 0 to just verify that the benchmark runs to
# completion.  We're interacting with two different flag parsers, so the order
# of flags and the -- matters.
"${TEST_SRCDIR}/envoy/${1}" "${@:2}" --skip_expensive_benchmarks -- --benchmark_min_time=0s
