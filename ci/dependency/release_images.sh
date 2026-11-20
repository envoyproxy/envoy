#!/usr/bin/env bash

# Update the base images pinned for the release container images to the current digest of their
# tag, and describe the result as JSON (`output`, `title`, `summary`) for the workflow to name the
# branch and PR.
#
# The pinned images are the `oci.pull` entries of `MODULE.bazel` with a `# tag: <tag>` comment: the
# images (`//distribution/docker`) are built with rules_oci from their digests.

set -e -o pipefail

MODULE_BAZEL="${MODULE_BAZEL:-MODULE.bazel}"

fail() {
    echo "::error::$*" >&2
    exit 1
}

# The pinned images as `name image:tag sha` lines.
pinned_images() {
    awk -F'"' '
        /^oci\.pull\(/ { pull = 1; name = image = tag = sha = ""; next }
        pull && /^\)/ {
            if (tag != "") print name, image ":" tag, sha
            pull = 0
        }
        pull && /^ +# tag: / { tag = $0; sub(/^ +# tag: +/, "", tag); sub(/ +$/, "", tag) }
        pull && /^ +name = "/ { name = $2 }
        pull && /^ +image = "/ { image = $2 }
        pull && /^ +digest = "sha256:/ { sha = $2; sub(/^sha256:/, "", sha) }
    ' "$MODULE_BAZEL"
}

# The current digest of an image tag (of the multi-arch index, where there is one).
image_sha() {
    docker buildx imagetools inspect "$1" < /dev/null \
        | sed -n -E 's/^Digest:[[:space:]]+sha256:([0-9a-f]{64})$/\1/p'
}

pinned=()
while read -r line; do
    [[ -n "$line" ]] && pinned+=("$line")
done < <(pinned_images)
(( ${#pinned[@]} > 0 )) || fail "No pinned images found in ${MODULE_BAZEL}"

changed=()
summary=""
for line in "${pinned[@]}"; do
    read -r name image current <<< "$line"
    [[ "$current" =~ ^[0-9a-f]{64}$ ]] || fail "${name} (${image}) has no sha256 digest in ${MODULE_BAZEL}"
    latest="$(image_sha "$image")"
    [[ -n "$latest" ]] || fail "Unable to resolve the digest of ${image}"
    if [[ "$latest" == "$current" ]]; then
        echo "${image}: up to date (${current:0:7})" >&2
        continue
    fi
    echo "${image}: ${current:0:7} -> ${latest:0:7}" >&2
    sed -i "s/${current}/${latest}/" "$MODULE_BAZEL"
    # `ubuntu_base` -> `ubuntu`
    changed+=("${name%%_*}-${latest:0:7}")
    summary+="- \`${image}\`: ${current:0:7} -> ${latest:0:7}"$'\n'
done

if (( ${#changed[@]} == 0 )); then
    echo 'Release images are up to date, nothing to do' >&2
    output=up-to-date
    title="docker/release: Bump container images"
elif (( ${#changed[@]} == 1 )); then
    output="${changed[0]}"
    title="docker/release: Bump ${output%-*} -> ${output##*-}"
else
    output="$(IFS=+; echo "${changed[*]}")"
    title="docker/release: Bump container images"
fi

jq -n -c \
   --arg output "$output" \
   --arg title "$title" \
   --arg summary "$summary" \
   '{output: $output, title: $title, summary: $summary}'
