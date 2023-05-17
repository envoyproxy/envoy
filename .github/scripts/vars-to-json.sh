#!/bin/bash -e

JQFILE="$(dirname "${BASH_SOURCE[0]}")/vars.jq"

jq -n -R -f "$JQFILE" "$GITHUB_OUTPUT"
