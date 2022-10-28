#!/bin/bash -e

RELEASE_VERSION="$1"
PUBLISH_DIR="$2"

REPO_OWNER="${REPO_OWNER:-envoyproxy}"
REPO_NAME="${REPO_NAME:-envoy}"
RELEASE_API_URL="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases"


sign_assets () {
    local asset

    rm -f checksums.txt

    for asset in ./*; do
        asset="$(echo "${asset}" | cut -d/ -f2)"
        if [[ "$asset" =~ ^checksums.txt ]]; then
            continue
        fi
        sha256sum "$asset" >> "checksums.txt"
    done

    gpg --clearsign checksums.txt
    rm checksums.txt
    cat checksums.txt.asc
}

get_release_id () {
    local url="${RELEASE_API_URL}/tags/${1}"
    curl \
        -s \
        -X GET \
        -H "Accept: application/vnd.github+json" \
        -H "Authorization: Bearer ${GITHUB_TOKEN}" \
        "${url}" \
        | jq '.id'
}

get_upload_url () {
    local url="${RELEASE_API_URL}/${1}"
    curl \
        -s \
        -X GET \
        -H "Accept: application/vnd.github+json" \
        -H "Authorization: Bearer ${GITHUB_TOKEN}" \
        "${url}" \
        | jq -r '.upload_url'
}

upload_to_github () {
    local upload_url="$1" \
          binary="$2"
    upload_url="$(echo "$upload_url" | cut -d\{ -f1)"
    echo -n "Uploading ${binary} ... "
    curl \
        -s \
        -X POST \
        -H "Content-Type: application/octet-stream" \
        -H "Accept: application/vnd.github+json" \
        -H "Authorization: Bearer ${GITHUB_TOKEN}" \
        --data-binary "@${binary}" \
        "${upload_url}?name=${binary}" \
            | jq -r '.state'
}

upload_assets () {
    local release_id upload_url
    release_id="$(get_release_id "${1}")"
    upload_url="$(get_upload_url "$release_id")"

    echo "Upload assets (${PUBLISH_DIR}) -> ${upload_url}"

    for asset in ./*; do
        asset="$(echo "${asset}" | cut -d/ -f2)"
        upload_to_github "${upload_url}" "$asset"
    done
}

cd "$PUBLISH_DIR" || exit 1
sign_assets
upload_assets "${RELEASE_VERSION}"
