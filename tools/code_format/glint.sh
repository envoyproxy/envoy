#!/bin/bash

NOLINT_RE="\.patch$|^test/.*_corpus/|^tools/.*_corpus/|password_protected_password.txt"
ERRORS=
MISSING_NEWLINE=0
MIXED_TABS_AND_SPACES=0
TRAILING_WHITESPACE=0


check_mixed_tabs_spaces () {
    local spaced tabbed
    tabbed=$(grep -cP "^\t" "$1")
    spaced=$(grep -cP "^ " "$1")
    if [[ $tabbed -gt 0 ]] && [[ $spaced -gt 0 ]]; then
        echo "mixed tabs and spaces: ${1}" >&2
        ERRORS=yes
        ((MIXED_TABS_AND_SPACES=MIXED_TABS_AND_SPACES+1))
    fi
}

check_new_line () {
    test "$(tail -c 1 "$1" | wc -l)" -eq 0 && {
        echo "no newline at eof: ${1}" >&2
        ERRORS=yes
        ((MISSING_NEWLINE=MISSING_NEWLINE+1))
    }
}

check_trailing_whitespace () {
    if grep -r '[[:blank:]]$' "$1" > /dev/null; then
        echo "trailing whitespace: ${1}" >&2
        ERRORS=yes
        ((TRAILING_WHITESPACE=TRAILING_WHITESPACE+1))
    fi
}

find_text_files () {
    git grep --cached -Il '' | grep -vE "$NOLINT_RE"
}

for file in $(find_text_files); do
    check_new_line "$file"
    check_mixed_tabs_spaces "$file"
    check_trailing_whitespace "$file"
done

if [[ -n "$ERRORS" ]]; then
    echo >&2
    echo "ERRORS found" >&2
    echo "${MISSING_NEWLINE} files with missing newline" >&2
    echo "${MIXED_TABS_AND_SPACES} files with mixed tabs and spaces" >&2
    echo "${TRAILING_WHITESPACE} files with trailing whitespace" >&2
    echo >&2
    exit 1
fi
