#!/bin/bash

# No core dumps.
ulimit -c 0

# Memory limits to avoid crashing the whole system if clangd goes wild.
exec prlimit --as=7000000000:8000000000 --rss=7000000000:8000000000 clangd "$@"
