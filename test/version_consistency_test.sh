#!/bin/bash
set -e -o pipefail

grep --fixed-strings $(cat external/envoy_api/VERSION) RAW_RELEASE_NOTES.md
