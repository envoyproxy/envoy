#!/usr/bin/env bash
# Generates a deterministic binary seed corpus file for json_sanitizer_fuzz_test.
#
# The output is designed to exercise the non-UTF-8 / octal-escape fallback path
# of Envoy::Json::sanitize(). libFuzzer mutates from this seed.
#
# Usage: gen_binary_corpus.sh <output_path>
set -euo pipefail
export LC_ALL=C

OUT="$1"

{
    # All 256 single-byte values (\x00..\xff) — covers every single-byte input.
    for ((i = 0; i < 256; i++)); do
        printf '%b' "\\0$(printf '%o' "$i")"
    done

    # Truncated multi-byte UTF-8 starters (missing continuation bytes).
    printf '\xc2'          # truncated 2-byte starter
    printf '\xe0\xa4'      # truncated 3-byte starter
    printf '\xf0\x9d\x84'  # truncated 4-byte starter (treble clef prefix)

    # Invalid continuation byte.
    printf '\xc2\xff'

    # Surrogate-range 3-byte encoding — always invalid UTF-8.
    printf '\xed\xa0\x80'

    # Overlong encoding of NUL.
    printf '\xc0\x80'

    # JSON-significant ASCII (" \ control chars \x00..\x1f \x7f) followed by
    # high-bit bytes \x80..\xff to trigger the octal-escape path on every value.
    printf '"'
    printf '%b' $'\\\\'
    for ((i = 0; i < 32; i++)); do
        printf '%b' "\\0$(printf '%o' "$i")"
    done
    printf '\x7f'
    for ((i = 128; i < 256; i++)); do
        printf '%b' "\\0$(printf '%o' "$i")"
    done

    # High-bit bytes embedded in an ASCII context.
    printf 'Hello, \xff\xfe\xfd, world!'

    # Deterministic pseudo-random tail: b[i] = (b[i-1] * 31 + 7) & 0xff
    # Seed: last byte of prefix above is '!' (ASCII 0x21 = 33).
    # Prefix byte count:
    #   256 (all bytes) + 6 (truncated starters: 1+2+3) + 2 (invalid cont.)
    #   + 3 (surrogate) + 2 (overlong NUL)
    #   + 163 (JSON section: 1 quote + 1 backslash + 32 controls + 1 DEL + 128 high)
    #   + 18 (Hello world) = 450 bytes
    # Tail byte count needed to reach target of 5698: 5698 - 450 = 5248
    val=33
    for ((i = 0; i < 5248; i++)); do
        val=$(( (val * 31 + 7) & 255 ))
        printf '%b' "\\0$(printf '%o' "$val")"
    done

} > "$OUT"
