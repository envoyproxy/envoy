#!/bin/bash

# Set the benchmark time to 0 to just verify that the benchmark runs to completion.
"$@" --benchmark_min_time=0
