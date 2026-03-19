#!/usr/bin/env bash

set -e
set -o pipefail

# Checks the absolute size and the relative size increase of a file.

# As of February 2026, the binary size is approximately 5,721,744 bytes (~5.72MB).
# MAX_SIZE is set with ~180KB headroom above that measurement.
MAX_SIZE=5900000 # 5.9MB
# MAX_PERC_HUNDREDTHS represents the maximum allowed percentage increase, in hundredths of a
# percent (150 = 1.50%). This single variable is the source of truth for both the comparison
# and the display string, so updating it here affects the actual check.
MAX_PERC_HUNDREDTHS=150

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
# Use absolute value for display so negative results don't produce garbled output (e.g. "0.-50")
if [[ $PERC_SCALED -lt 0 ]]; then
    PERC_ABS=$(( -PERC_SCALED ))
    PERC_SIGN="-"
else
    PERC_ABS=$PERC_SCALED
    PERC_SIGN=""
fi
PERC_INT=$(( PERC_ABS / 100 ))
PERC_DEC=$(( PERC_ABS % 100 ))
printf -v PERC "%s%d.%02d" "$PERC_SIGN" "$PERC_INT" "$PERC_DEC"

# Format MAX_PERC for display from the single source-of-truth variable
MAX_PERC_INT=$(( MAX_PERC_HUNDREDTHS / 100 ))
MAX_PERC_DEC=$(( MAX_PERC_HUNDREDTHS % 100 ))
printf -v MAX_PERC "%d.%02d" "$MAX_PERC_INT" "$MAX_PERC_DEC"

echo "The new binary is $PERC % different in size compared to main."
echo "The old binary is $SIZE1 bytes."
echo "The new binary is $SIZE2 bytes."

if [[ $SIZE2 -gt $MAX_SIZE ]]; then
    echo "The current size ($SIZE2) is larger than the maximum size ($MAX_SIZE)."
    exit 1
fi

if [[ $PERC_SCALED -ge $MAX_PERC_HUNDREDTHS ]]; then
    echo "The percentage increase ($PERC) is larger than the maximum percentage increase ($MAX_PERC)."
    exit 1
fi
