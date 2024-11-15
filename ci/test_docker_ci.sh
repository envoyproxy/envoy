#!/usr/bin/env bash

# Run this with `./ci/test_docker_ci.sh`.
#
# Compares against data stored in `ci/test/docker`
#
# To commit what is currently produced use:
#    `DOCKER_CI_TEST_COMMIT=1 ./ci/test_docker_ci.sh`
#

TESTS_MATCHING="$1"

FAILED=()
DOCKER_CI_FIX="${DOCKER_CI_FIX:-}"
DOCKER_CI_FIX_DIFF="${DOCKER_CI_FIX_DIFF:-}"

VERSION="1.73"
RELEASE_BRANCH="refs/heads/release/vXXX"
TAG_BRANCH="refs/tags/vXXX"
MAIN_BRANCH="refs/heads/main"
OTHER_BRANCH="refs/heads/something/else"

PLATFORMS=(linux)
TEST_TYPES=(dev nondev)
BRANCH_TYPES=(tag release main other)


_test () {
    local test_type="$1" branch_type="$2" version branch platform name testdata
    local platform="${3:-linux}"

    name="${platform}_${test_type}_${branch_type}"
    testdata="ci/test/docker/${platform}/${test_type}/${branch_type}"

    if ! test_matches "${name}"; then
        return
    fi

    if [[ "$branch_type" == "release" ]]; then
       version="${VERSION}.3"
       branch="$RELEASE_BRANCH"
    elif [[ "$branch_type" == "tag" ]]; then
       version="${VERSION}.3"
       branch="$TAG_BRANCH"
    elif [[ "$branch_type" == "other" ]]; then
       version="${VERSION}.3"
       branch="$OTHER_BRANCH"
    else
       branch="$MAIN_BRANCH"
       version="${VERSION}.0"
    fi
    if [[ "$test_type" == "dev" ]]; then
        version="${version}-dev"
    fi

    export ENVOY_VERSION="${version}"
    export CI_BRANCH="$branch"
    # this should be ignored if the non-push
    export DOCKERHUB_USERNAME=DHUSER
    export DOCKERHUB_PASSWORD=DHPASSWORD
    export DOCKER_CI_DRYRUN=1
    export ENVOY_DOCKER_IMAGE_DIRECTORY=/non/existent/test/path
    export DOCKER_IMAGE_PREFIX=mocktest/repo

    if [[ "$DOCKER_CI_TEST_COMMIT" ]]; then
        echo "COMMIT(${name}): > ${testdata}"
        echo "  ENVOY_VERSION=${version} ENVOY_DOCKER_IMAGE_DIRECTORY=/non/existent/test/path CI_BRANCH=${branch} DOCKER_CI_DRYRUN=1 ./ci/docker_ci.sh | grep -E \"^>\""
        ./ci/docker_ci.sh | grep -E "^>" > "$testdata"
        return
    fi

    echo "TEST(${name}): <> ${testdata}"
    echo "  ENVOY_VERSION=${version} ENVOY_DOCKER_IMAGE_DIRECTORY=/non/existent/test/path CI_BRANCH=${branch} DOCKER_CI_DRYRUN=1 ./ci/docker_ci.sh | grep -E \"^>\""
    generated="$(mktemp)"

    ./ci/docker_ci.sh | grep -E "^>" > "$generated"

    cmp --silent "$testdata" "$generated" || {
        echo "files are different" >&2
        diff "$testdata" "$generated" >&2
        echo >&2
        echo "--------------------------" >&2
        cat "$generated"
        echo >&2
        FAILED+=("$name")
        echo >&2
        echo "--------------------------" >&2
    }

    rm "$generated"
}

test_matches () {
    local test_type="$1"
    if [[ -z "$TESTS_MATCHING" ]]; then
        return 0
    fi
    if [[ "$test_type" =~ $TESTS_MATCHING ]]; then
        return 0
    fi
    return 1
}

run_tests () {
    local platform test_type branch_type

    for platform in "${PLATFORMS[@]}"; do
        for test_type in "${TEST_TYPES[@]}"; do
            for branch_type in "${BRANCH_TYPES[@]}"; do
                _test "$test_type" "$branch_type" "$platform"
            done
        done
    done
}

handle_failure () {
    local platform test_type branch_type
    if [[ "${#FAILED[@]}" -eq 0 ]]; then
        return
    fi
    echo >&2
    echo "----------------------" >&2
    echo "FAILED" >&2

    for FAILURE in "${FAILED[@]}"; do
        echo "$FAILURE" >&2
        if [[ -n "$DOCKER_CI_FIX" ]]; then
            platform="$(echo "$FAILURE" | cut -d_ -f1)"
            test_type="$(echo "$FAILURE" | cut -d_ -f2)"
            branch_type="$(echo "$FAILURE" | cut -d_ -f3)"
            DOCKER_CI_TEST_COMMIT=1 _test "$test_type" "$branch_type" "$platform"
        fi
    done

    if [[ -n "$DOCKER_CI_FIX" ]]; then
        git add -N ci/test/docker
        echo >&2
        echo "----------------------" >&2
        echo "DIFF APPLIED" >&2
        echo >&2
        git diff ci/test/docker >&2

        if [[ -n "$DOCKER_CI_FIX_DIFF" ]]; then
            git diff ci/test/docker > "$DOCKER_CI_FIX_DIFF"
        fi
        echo "----------------------" >&2
        if [[ -e "$DOCKER_CI_FIX_DIFF" ]]; then
            echo >&2
            echo "Diff file with fixes will be uploaded. Please check the artefacts for this PR run in the azure pipeline." >&2
            echo >&2
            echo "Fixes will need to be checked before being committed." >&2
        fi
    fi

    return 1
}

run_tests
handle_failure || {
    exit 1
}
