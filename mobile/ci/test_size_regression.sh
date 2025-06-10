#!/usr/bin/env bash

set -e
set -o pipefail

# Checks the absolute size and the relative size increase of a file.

# As of May 7, 2024, the latest runs show that the test binary size is
# 5413199 bytes:
# https://github.com/envoyproxy/envoy/actions/runs/8990218789/job/24695250246
MAX_SIZE=5600000 # 5.6MB
MAX_PERC=1.5

if [[ "$(uname)" == "Darwin" ]]; then
    SIZE1=$(stat -f "%z" "$1")
    SIZE2=$(stat -f "%z" "$2")
else
    SIZE1=$(stat -c "%s" "$1")
    SIZE2=$(stat -c "%s" "$2")
fi
# Calculate percentage difference using bash arithmetic
# Use fixed-point arithmetic: multiply by 10000 to get 4 decimal places, then format to 2
PERC_SCALED=$(( (SIZE2 - SIZE1) * 10000 / SIZE1 ))
PERC_INT=$(( PERC_SCALED / 100 ))
PERC_DEC=$(( PERC_SCALED % 100 ))

printf -v PERC "%d.%02d" "$PERC_INT" "$PERC_DEC"

echo "The new binary is $PERC % different in size compared to main."
echo "The old binary is $SIZE1 bytes."
echo "The new binary is $SIZE2 bytes."

if [[ $SIZE2 -gt $MAX_SIZE ]]; then
    echo "The current size ($SIZE2) is larger than the maximum size ($MAX_SIZE)."
    exit 1
fi

MAX_PERC_SCALED=150
if [[ $PERC_SCALED -ge $MAX_PERC_SCALED ]]; then
    echo "The percentage increase ($PERC) is larger then the maximum percentage increase ($MAX_PERC)."
    exit 1
fi
