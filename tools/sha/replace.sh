#!/bin/bash -e

set -o pipefail

# This tool is for replacing shas in the repo, altho it could be
# used to replace any strings.
#
# It does not currently validate that target/replacements are any kind of sha.

REPO_PATH="$1"
shift

cd "$REPO_PATH" || exit 1

for arg in "$@"; do
    target="$(echo "$arg" | cut -d: -f1)"
    replacement="$(echo "$arg" | cut -d: -f2)"
    echo "Replacing ${target} -> ${replacement}"
    git grep "$target" \
        | cut -d: -f1 \
        | xargs sed -i "s/${target}/${replacement}/g" || {
        echo "No shas replaced for ${target}" >&2
    }
done
