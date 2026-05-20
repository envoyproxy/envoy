#!/usr/bin/env bash

set -e
set -o pipefail


DEP_CHECK="${DEP_CHECK:-tools/dependency/check}"

# Derive the companion deps.yaml from VERSION_FILE.
# VERSION_FILE is always <dir>/repository_locations.bzl, so:
#   bazel/repository_locations.bzl     -> bazel/deps.yaml
#   api/bazel/repository_locations.bzl -> api/bazel/deps.yaml
DEPS_YAML="${VERSION_FILE%/repository_locations.bzl}/deps.yaml"

if [[ ! -f "$DEPS_YAML" ]]; then
    echo "ERROR: Expected deps YAML not found: ${DEPS_YAML}" >&2
    exit 1
fi

# Read the current release_date directly from the dependency JSON data.
# shellcheck disable=SC2016
EXISTING_DATE="$("$JQ" -r --arg dep "$DEP" '.[$dep].release_date' "$DEP_DATA")"

if [[ -z "$EXISTING_DATE" || "$EXISTING_DATE" == "null" ]]; then
    echo "ERROR: Could not find release_date for '${DEP}' in DEP_DATA" >&2
    exit 1
fi

find_date_line () {
    local dep_ln date_match_ln
    # Find the line number of the dep's top-level YAML key (e.g. "bazel_gazelle:").
    dep_ln="$(grep -n "^${DEP}:$" "$DEPS_YAML" | head -n1 | cut -d: -f1)"
    if [[ -z "$dep_ln" ]]; then
        echo "ERROR: Could not find dep block '${DEP}:' in ${DEPS_YAML}" >&2
        exit 1
    fi
    # From that line forward, find the first matching release_date line.
    date_match_ln="$(\
        tail -n "+${dep_ln}" "$DEPS_YAML" \
        | grep -n "^  release_date: \"${EXISTING_DATE}\"$" \
        | head -n1 \
        | cut -d: -f1)"
    if [[ -z "$date_match_ln" ]]; then
        echo "ERROR: Could not find release_date line for '${DEP}' in ${DEPS_YAML}" >&2
        exit 1
    fi
    printf '%s' "$((dep_ln + date_match_ln - 1))"
}

update_date () {
    local match_ln search replace
    match_ln="$1"
    search="$2"
    replace="$3"
    echo "Updating date(${match_ln}): ${search} -> ${replace}"
    sed -i "${match_ln}s/${search}/${replace}/" "$DEPS_YAML"
}

get_new_date () {
    # create a repository_locations with just the dep and with updated version
    tmpfile="$(mktemp)"
    # shellcheck disable=SC2016
    "$JQ" --arg new_version "$VERSION" \
       --arg existing_version "$EXISTING_VERSION" \
       --arg dep "$DEP" \
       'if has($dep) then .[$dep].version = $new_version | .[$dep].urls |= map(gsub($existing_version; $new_version)) else . end' \
       "$DEP_DATA" > "$tmpfile"
    output="$(\
      "$DEP_CHECK" \
        --repository_locations="$tmpfile" \
        --path "${BUILD_WORKSPACE_DIRECTORY}" \
        -c release_dates 2>&1)"
    echo "$output" \
        | grep -E "^Mismatch" \
        | grep "$DEP" \
        | cut -d= -f2 \
        | xargs || {
        cat "$tmpfile" >&2
        echo "$output" >&2
        rm "$tmpfile"
        exit 1
    }
    rm "$tmpfile"
}

post_version_update () {
    local date_ln new_date
    if [[ "$EXISTING_VERSION" == "$VERSION" ]]; then
        echo "Nothing to update" >&2
        exit 0
    fi
    date_ln="$(find_date_line)"
    new_date="$(get_new_date)"
    if [[ -z "$new_date" ]]; then
        echo "Unable to retrieve date" >&2
        exit 1
    fi
    update_date "$date_ln" "$EXISTING_DATE" "$new_date"
}
