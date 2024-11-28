#!/usr/bin/env bash

set -eo pipefail

WORKSPACE="${1}"
WORKSPACE_ROOT="$(realpath "${WORKSPACE}/..")"
PATCHES_PATH="${2}"
PATCH_README_TPL="$(realpath "${PATCH_README_TPL}")"
ENVOY_VERSION_COUNT=4

create_patches_for_version () {
    # Create patches and README for a version
    local version="$1" patch_branch patch_dir patch_readme mirror_branch

    pushd "${WORKSPACE_ROOT}/envoy" > /dev/null || exit 1
    patch_dir="${PATCHES_PATH}/${version}"
    patch_readme="${patch_dir}/README.md"

    # Get the public Envoy branch name - eg `release/v1.25`
    if [[ "$version" == "main" ]]; then
        mirror_branch="envoy/main"
        patch_branch="patches/main"
    else
        mirror_branch="envoy/${version}"
        patch_branch="patches/${version}"
    fi

    # this expects there to be a SINGLE COMMIT extra on the envoy mirror branch
    last_commit="$(git log --skip=1 -n1 "origin/${mirror_branch}" --pretty=format:"%H")"
    patch_commit="$(git log -n1 "origin/${patch_branch}" --pretty=format:"%H")"
    patch_count="$(git rev-list --count "${last_commit}".."${patch_commit}")"

    echo "PATCH (${version}) from ${last_commit} -> ${patch_commit} (${patch_count} commits)"

    git checkout "origin/$patch_branch"
    git format-patch "-${patch_count}" -o "$patch_dir"

    # Generate a README from template
    echo "Creating README (${version}): ${PATCH_README_TPL} -> ${patch_readme}"
    PATCHES_FILENAME="$(basename "${PATCHES_PATH}")"
    PATCHES_FILENAME="$PATCHES_FILENAME.zip" \
             PATCH_BRANCH="$patch_branch" \
             PATCH_COMMIT="$last_commit" \
             PATCH_VERSION="$version" \
                 envsubst < "$PATCH_README_TPL" > "$patch_readme"
    echo "-----------------------"
    cat "$patch_readme"
    echo
    popd > /dev/null
}

create_patches () {
    local current_version current_minor first_minor version
    current_version="${1}"
    current_minor="$(echo "${current_version}" | cut -d. -f2)"
    current_minor="$((current_minor-1))"
    first_minor="$((current_minor-ENVOY_VERSION_COUNT+1))"

    # Iterate patch versions
    for (( minor="${first_minor}"; minor<="${current_minor}"; minor++ )); do
        create_patches_for_version "1.${minor}"
    done

    # Patches for `main`
    create_patches_for_version main
}

get_version () {
    pushd "${WORKSPACE_ROOT}/envoy" > /dev/null || exit 1
    git show origin/envoy/main:VERSION.txt
    popd > /dev/null
}

create_patch_set () {
    local version
    version=$(get_version)
    create_patches "${version}"

    # Add root README
    echo "Please see READMEs inside each folder for Envoy branch patching instructions" > "${PATCHES_PATH}/README.md"
}

create_patch_set
