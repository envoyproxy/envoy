#!/usr/bin/env bash

output="$("$@" 2>&1)"
status=$?

if [[ "$status" -ne 0 || -n "$output" ]]; then
    echo "$output"
    exit 1
fi
