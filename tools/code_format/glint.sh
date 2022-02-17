#!/bin/bash

# This script checks all files in the repo for basic format "hygiene", specifically
#
# - must have ending line
# - no trailing whitespace
# - no lines indented with a mixture of tabs and spaces
#

NOLINT_RE="\.patch$|^test/.*_corpus/|^tools/.*_corpus/|password_protected_password.txt"
ERRORS=
MISSING_NEWLINE=0
MIXED_TABS_AND_SPACES=0
TRAILING_WHITESPACE=0
# AZP appears to make lines with this prefix red
BASH_ERR_PREFIX="##[error]: "


# Checks whether a file has a mixture of indents starting with tabs and spaces
check_mixed_tabs_spaces () {
    local spaced tabbed
    tabbed=$(grep -cP "^\t" "$1")
    spaced=$(grep -cP "^ " "$1")
    if [[ $tabbed -gt 0 ]] && [[ $spaced -gt 0 ]]; then
        echo "${BASH_ERR_PREFIX}mixed tabs and spaces: ${1}" >&2
        ERRORS=yes
        ((MIXED_TABS_AND_SPACES=MIXED_TABS_AND_SPACES+1))
    fi
}

# Checks whether a file has a terminating newline
check_new_line () {
    test "$(tail -c 1 "$1" | wc -l)" -eq 0 && {
        echo "${BASH_ERR_PREFIX}no newline at eof: ${1}" >&2
        ERRORS=yes
        ((MISSING_NEWLINE=MISSING_NEWLINE+1))
    }
}

# Checks whether a file contains lines ending in whitespace
check_trailing_whitespace () {
    if grep -r '[[:blank:]]$' "$1" > /dev/null; then
        echo "${BASH_ERR_PREFIX}trailing whitespace: ${1}" >&2
        ERRORS=yes
        ((TRAILING_WHITESPACE=TRAILING_WHITESPACE+1))
    fi
}

# Uses git grep to search for non-"binary" files from git's pov
#
# TODO(phlax): add hash/diff only filter for faster change linting
#      this would also make it feasible to add as a commit/push hook
find_text_files () {
    git grep --cached -Il '' | grep -vE "$NOLINT_RE"
}

# Recurse text files linting language-independent checks
#
# note: we may want to use python if this grows in complexity
#
for file in $(find_text_files); do
    check_new_line "$file"
    check_mixed_tabs_spaces "$file"
    check_trailing_whitespace "$file"
done

if [[ -n "$ERRORS" ]]; then
    echo >&2
    echo "${BASH_ERR_PREFIX}ERRORS found" >&2
    echo "${BASH_ERR_PREFIX}${MISSING_NEWLINE} files with missing newline" >&2
    echo "${BASH_ERR_PREFIX}${MIXED_TABS_AND_SPACES} files with mixed tabs and spaces" >&2
    echo "${BASH_ERR_PREFIX}${TRAILING_WHITESPACE} files with trailing whitespace" >&2
    echo >&2
    exit 1
fi
