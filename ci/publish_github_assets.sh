#!/bin/bash -e

RELEASE_VERSION="$1"
PUBLISH_DIR="$2"

REPO_OWNER="${REPO_OWNER:-envoyproxy}"
REPO_NAME="${REPO_NAME:-envoy}"
RELEASE_API_URL="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases"
MAINTAINER_GPG_KEY_PASSPHRASE="${MAINTAINER_GPG_KEY_PASSPHRASE:-}"
GITHUB_TOKEN="${GITHUB_TOKEN:-}"

if [[ -z "$GITHUB_TOKEN" ]]; then
    # shellcheck disable=SC2016
    echo 'env var `GITHUB_TOKEN` must be set'
    exit 1
fi


gpg_sign () {
    if [[ -n "$MAINTAINER_GPG_KEY_PASSPHRASE" ]]; then
        echo "$MAINTAINER_GPG_KEY_PASSPHRASE" | gpg --pinentry-mode loopback --passphrase-fd 0 --clearsign checksums.txt
    else
        gpg --clearsign checksums.txt
    fi
}

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

    gpg_sign checksums.txt
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
    if [[ "$release_id" == null ]]; then
        # shellcheck disable=SC2016
        echo 'Failed querying github API - `GITHUB_TOKEN` may not be valid or the release ('"${release_id}"') was not found'
        return 1
    fi
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
