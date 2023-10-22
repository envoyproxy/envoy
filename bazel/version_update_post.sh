#!/bin/bash -e

set -o pipefail


EXISTING_DATE="$("${JQ}" -r ".${DEP}.release_date" "${DEP_DATA}")"
DATE_SEARCH="release_date = \"${EXISTING_DATE}\","
DEP_CHECK="${DEP_CHECK:-tools/dependency/check}"

find_date_line () {
    local match match_ln date_match_ln
    # This needs to find the correct date to replace
    match="$(\
        grep -n "${DEP_SEARCH}" "${VERSION_FILE}" \
        | cut -d: -f-2)"
    match_ln="$(\
        echo "${match}" \
        | cut -d: -f1)"
    match_ln="$((match_ln + 1))"
    date_match_ln="$(\
        tail -n "+${match_ln}" "${VERSION_FILE}" \
        | grep -n "${DATE_SEARCH}" \
        | head -n1 \
        | cut -d: -f1)"
    date_match_ln="$((match_ln + date_match_ln - 1))"
    printf '%s' "$date_match_ln"
}

update_date () {
    local match_ln search replace
    match_ln="$1"
    search="$2"
    replace="$3"
    echo "Updating date(${match_ln}): ${search} -> ${replace}"
    sed -i "${match_ln}s/${search}/${replace}/" "$VERSION_FILE"
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
