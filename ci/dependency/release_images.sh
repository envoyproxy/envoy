#!/usr/bin/env bash

# Update the base images pinned in the release Dockerfile to the current digest of their tag,
# and describe the result as JSON (`output`, `title`, `summary`) for the workflow to name the
# branch and PR.
#
# The pinned images are the `BUILD_OS:BUILD_TAG@sha256:BUILD_SHA` args, and any
# `FROM image:tag@sha256:... AS envoy-<name>` stage.

set -e -o pipefail

DOCKERFILE="${DOCKERFILE:-distribution/docker/Dockerfile-envoy}"

fail() {
    echo "::error::$*" >&2
    exit 1
}

dockerfile_arg() {
    sed -n -E "s/^ARG ${1}=(.+)$/\1/p" "$DOCKERFILE"
}

# The pinned images as `name image:tag sha` lines.
pinned_images() {
    local build_os build_sha build_tag

    build_os="$(dockerfile_arg BUILD_OS)"
    build_tag="$(dockerfile_arg BUILD_TAG)"
    build_sha="$(dockerfile_arg BUILD_SHA)"
    if [[ -n "$build_os" && -n "$build_tag" && -n "$build_sha" ]]; then
        echo "${build_os} ${build_os}:${build_tag} ${build_sha}"
    fi
    sed -n -E \
        's/^FROM ([^@ ]+)@sha256:([0-9a-f]{64}) AS envoy-([^ ]+)$/\3 \1 \2/p' \
        "$DOCKERFILE"
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
(( ${#pinned[@]} > 0 )) || fail "No pinned images found in ${DOCKERFILE}"

changed=()
summary=""
for line in "${pinned[@]}"; do
    read -r name image current <<< "$line"
    latest="$(image_sha "$image")"
    [[ -n "$latest" ]] || fail "Unable to resolve the digest of ${image}"
    if [[ "$latest" == "$current" ]]; then
        echo "${image}: up to date (${current:0:7})" >&2
        continue
    fi
    echo "${image}: ${current:0:7} -> ${latest:0:7}" >&2
    sed -i "s/${current}/${latest}/" "$DOCKERFILE"
    changed+=("${name}-${latest:0:7}")
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
