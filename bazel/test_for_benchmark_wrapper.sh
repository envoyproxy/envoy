#!/bin/bash

# Set the benchmark time to a tiny value since we just want to know if the benchmark runs to completion.
HEAPCHECK= "$@" --benchmark_min_time=0.00001
