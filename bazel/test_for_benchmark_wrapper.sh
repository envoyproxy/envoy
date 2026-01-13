#!/usr/bin/env bash

# Set the benchmark time to 0 to just verify that the benchmark runs to
# completion.  We're interacting with two different flag parsers, so the order
# of flags and the -- matters.

# TODO(phlax): Cleanup once bzlmod migration is complete

# In bzlmod mode, the workspace name is _main instead of envoy
# Try both paths to support both WORKSPACE and bzlmod modes
if [[ -f "${TEST_SRCDIR}/_main/${1}" ]]; then
    BENCHMARK_BINARY="${TEST_SRCDIR}/_main/${1}"
elif [[ -f "${TEST_SRCDIR}/envoy/${1}" ]]; then
    BENCHMARK_BINARY="${TEST_SRCDIR}/envoy/${1}"
else
    echo "Error: Could not find benchmark binary at ${TEST_SRCDIR}/_main/${1} or ${TEST_SRCDIR}/envoy/${1}" >&2
    exit 1
fi

"${BENCHMARK_BINARY}" "${@:2}" --skip_expensive_benchmarks -- --benchmark_min_time=0s
