#!/bin/bash

# Set the benchmark time to 0 to just verify that the benchmark runs to completion.
"${TEST_SRCDIR}/envoy/$1" --benchmark_min_time=0
