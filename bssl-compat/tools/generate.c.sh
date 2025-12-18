#!/bin/bash

set -euo pipefail
# set -x # Echo commands

#
# For a specified function, this script will find the signature in the relevant
# BoringSSL header, and generate an implementation for it which calls directly
# onto the equivalent OpenSSL function (via the dynamically loaded
# ossl.ossl<function> pointer)
#
# This is a super naive implementation that doesn't work for all functions
# because it only uses simple text processing to search/process the source &
# header files. For example, simple text searching cannot find some functions in
# the BoringSSL headers because they are declared by macros that construct the
# function's name by concatination.
#
TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FUNC_NAME="${1?"FUNC_NAME not specified"}"
CC_FILE="${2?"CC_FILE not specified"}"


function error {
    cmake -E cmake_echo_color --red "$1"
    exit 1
}

# Accept third argument for include directory (for Bazel builds)
INCLUDE_DIR="${3?"INCLUDE_DIR not specified"}"
[[ -d "$INCLUDE_DIR" ]] || error "INCLUDE_DIR $INCLUDE_DIR does not exist"

################################################################################
# Find out which header file the function is declared in
################################################################################
HDR_FILE=$(grep -r "OPENSSL_EXPORT.*[^A-Za-z0-9_]$FUNC_NAME[ \t]*(" $INCLUDE_DIR/openssl/* | cut -d: -f1)
if [ ! -f "$HDR_FILE" ]; then
  error "Failed to determine header file for $FUNC_NAME"
fi

################################################################################
# Extract the function's line number(s), return type, args etc
################################################################################
FUNC_SIG_MULTI_LINE="$(grep -Pzob "OPENSSL_EXPORT\s.*[^A-Za-z0-9_]$FUNC_NAME\s*\([^;]*\)" "$HDR_FILE" | sed -e 's/OPENSSL_EXPORT\s*//g' -e 's|^// ||' -e 's/\x0//g')"
FUNC_SIG_LINE_COUNT="$(echo "$FUNC_SIG_MULTI_LINE" | wc -l)"
FUNC_SIG_OFFSET="$(echo "$FUNC_SIG_MULTI_LINE" | grep -o '^[0-9]*:' | cut -d: -f1)"
FUNC_SIG_ONE_LINE="$(echo "$FUNC_SIG_MULTI_LINE" | tr '\n' ' ' | sed -e 's/\s\s*/ /g' -e 's/\s*$//g' | cut -d: -f2)"
FUNC_SIG_LINE_FROM=$(($(head -c+$FUNC_SIG_OFFSET "$HDR_FILE" | wc -l) + 1))
FUNC_SIG_LINE_TO=$(($FUNC_SIG_LINE_FROM + $FUNC_SIG_LINE_COUNT - 1))
FUNC_SIG_RET="$(echo "$FUNC_SIG_ONE_LINE" | sed -e "s/\(.*[^A-Za-z_]\)$FUNC_NAME\s*(.*)$/\1/g")"
FUNC_SIG_ARGS="$(echo "$FUNC_SIG_ONE_LINE" | sed -e "s/.*[^A-Za-z0-9_]$FUNC_NAME\s*\((.*)\)$/\1/g")"
FUNC_SIG_ARGS_NAMES="($(echo $FUNC_SIG_ARGS | sed -e 's/^(//g' -e 's/)$//g' | \
                                              tr ',' '\n' | \
                                              sed -e 's/\[.*\]$//g' -e 's/^.*[^a-zA-Z0-9_]\([a-zA-Z_][a-zA-Z0-9_]*\)/\1/g' -e 's/\[.*\]$//g' | \
                                              tr '\n' ',' | \
                                              sed -e 's/,$//g' -e 's/,/, /g' -e 's/void//g'))"

################################################################################
# Generate the source file
#
# Note that if the OpenSSL function that we are calling onto is a function-like
# macro then we must call it by name so that it expands correctly. Otherwise, if
# it's a proper function, then call it via the dynamically loaded function
# pointer in the ossl struct. We _could_ detect which case it is by examining
# the OpenSSL headers, but it's much easier to use an #ifdef in the generated
# code.
################################################################################
cat <<- EOF > "$CC_FILE"
#include <openssl/$(basename "$HDR_FILE")>
#include <ossl.h>


$FUNC_SIG_ONE_LINE {
#ifdef ossl_$FUNC_NAME
  $([ "$FUNC_SIG_RET" != "void" ] && echo "return ")ossl_$FUNC_NAME$FUNC_SIG_ARGS_NAMES;
#else
  $([ "$FUNC_SIG_RET" != "void" ] && echo "return ")ossl.ossl_$FUNC_NAME$FUNC_SIG_ARGS_NAMES;
#endif
}
EOF
