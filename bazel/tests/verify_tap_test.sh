#!/usr/bin/env bash

set -ex

# Clear existing tap directory if previous run wasn't in sandbox
rm -rf tap

mkdir -p tap
TAP_TMP="$(realpath tap)"

TAP_PATH="${TAP_TMP}/tap" "$@"

# TODO(htuch): Check for pcap, now CI (with or without RBE) does have
# enough capabilities.
# Verify that some pb_text files have been created.
ls -l "${TAP_TMP}"/tap_*.pb_text > /dev/null
