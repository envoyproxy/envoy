#!/usr/bin/env bash

# Function library for bzlmod module/registry/deps/lockfile operations, sourced by
# ci/do_ci.sh and not meant to be executed directly. This expects _realpath to be
# defined before sourcing and relies on BAZEL_STARTUP_OPTIONS, BAZEL_BUILD_OPTIONS,
# and BAZEL_GLOBAL_OPTIONS from ci/build_setup.sh when the helpers are used.

ENVOY_DOCS_PATH="${ENVOY_DOCS_PATH:-./docs}"
ENVOY_DOCS_PATH="$(_realpath "$ENVOY_DOCS_PATH")"
LOCKFILES_DIFF_OUTPUT="${LOCKFILES_DIFF_OUTPUT:-/build/fix_lockfiles.diff}"
readonly LOCKFILE_PATHSPEC=':(glob)**/MODULE.bazel.lock'
readonly -a REGISTRY_BAZELRC_FILES=(
    ".bazelrc"
    "api/.bazelrc"
    "bazel/tests/codeql/.bazelrc"
    "bazel/tests/external/.bazelrc"
)
# shellcheck disable=SC2034
readonly -a MODULE_DIRS=(. "$ENVOY_DOCS_PATH" api mobile bazel/tests/codeql bazel/tests/external)
# shellcheck disable=SC2034
readonly -a REGISTRY_MODULE_DIRS=(. api bazel/tests/codeql bazel/tests/external)

run_in_mods() {
    local -n _mod_dirs="$1"
    local fn="$2"
    shift 2
    local module_dir status

    declare -F "$fn" > /dev/null || {
        echo "FAIL: no such function: ${fn}" >&2
        return 1
    }
    for module_dir in "${_mod_dirs[@]}"; do
        status=0
        pushd "$module_dir" > /dev/null || return 1
        "$fn" "$module_dir" "$@" || status=$?
        bazel "${BAZEL_STARTUP_OPTIONS[@]}" shutdown
        popd > /dev/null || return 1
        (( status == 0 )) || return "$status"
    done
}

run_in_nested_mods() {
    run_in_mods MODULE_DIRS "$@"
}

run_in_registry_mods() {
    run_in_mods REGISTRY_MODULE_DIRS "$@"
}

workspace_name() {
    local module_dir="${1%/}"

    case "$module_dir" in
        .) echo "root" ;;
        "$ENVOY_DOCS_PATH") echo "docs" ;;
        api) echo "api" ;;
        mobile) echo "mobile" ;;
        bazel/tests/external) echo "bazel/tests/external" ;;
        *) echo "$module_dir" ;;
    esac
}

workspace_target() {
    local module_dir="${1%/}"
    local target_name="$2"

    case "$module_dir" in
        .) echo "//bazel/dependency:${target_name}" ;;
        "$ENVOY_DOCS_PATH") echo "//:${target_name}" ;;
        api) echo "//bazel:${target_name}" ;;
        mobile) echo "//bazel:${target_name}" ;;
        bazel/tests/codeql) echo "//:${target_name}" ;;
        bazel/tests/external) echo "//:${target_name}" ;;
        *)
            echo "FAIL: Unknown workspace: ${module_dir}" >&2
            return 1
            ;;
    esac
}

workspace_bazel_run() {
    local module_dir="$1"
    local target_name="$2"
    local target
    shift 2

    target="$(workspace_target "$module_dir" "$target_name")" || return 1
    bazel run "${BAZEL_GLOBAL_OPTIONS[@]}" --config=ci "$target" "$@"
}

lockfiles_check() {
    lockfiles_generate
    if [[ -z "$(git status --porcelain -- "$LOCKFILE_PATHSPEC")" ]]; then
        return 0
    fi
    git --no-pager diff --stat -- "$LOCKFILE_PATHSPEC"
    echo >&2
    echo "FAIL: Lockfiles are not in sync, please run: ci/do_ci.sh lockfiles" >&2
    if { git --no-pager diff -- "$LOCKFILE_PATHSPEC" > "$LOCKFILES_DIFF_OUTPUT"; } 2>/dev/null; then
        echo "  Full diff written to ${LOCKFILES_DIFF_OUTPUT}" >&2
    fi
    echo >&2
    exit 1
}

lockfiles_generate() {
    run_in_nested_mods _lockfiles_generate_mod
}

_lockfiles_generate_mod() {
    bazel mod "${BAZEL_GLOBAL_OPTIONS[@]}" deps --config=ci --lockfile_mode=update
}

registry_current_hash() {
    local bazelrc
    local hash
    local current_hash=""

    for bazelrc in "${REGISTRY_BAZELRC_FILES[@]}"; do
        hash="$(sed -n -E \
            's#^common --registry=https://raw\.githubusercontent\.com/envoyproxy/bazel-registry/([0-9a-f]+)$#\1#p' \
            "$bazelrc")"
        if [[ -z "${hash}" ]]; then
            echo "FAIL: Failed to determine current registry hash from ${bazelrc}" >&2
            return 1
        fi
        if [[ -n "${current_hash}" && "${current_hash}" != "${hash}" ]]; then
            echo "FAIL: Registry hash mismatch: ${bazelrc} has ${hash}, expected ${current_hash}" >&2
            return 1
        fi
        current_hash="${hash}"
    done

    echo "${current_hash}"
}

registry_check() {
    local version
    local registry_hash=""

    version="$(cat VERSION.txt)"
    if [[ -n "${ENVOY_REGISTRY_ALLOW_UNSAFE:-}" ]]; then
        run_in_registry_mods _registry_check_mod \
            "--@envoy_toolshed//dependency:registry_allow_unsafe=true"
        return
    fi
    run_in_registry_mods _registry_check_mod
}

_registry_check_mod() {
    local module_dir="$1"
    local markdown_path
    local sha
    local sha_path
    local status_path
    local tags
    local workspace
    shift

    workspace="$(workspace_name "$module_dir")"
    status_path="$(mktemp)"
    markdown_path="$(mktemp)"
    sha_path="$(mktemp)"
    # shellcheck disable=SC2064
    trap "rm -f '${status_path}' '${markdown_path}' '${sha_path}'" RETURN

    workspace_bazel_run "$module_dir" update_registry.check "$@" -- \
        "--json-out=${status_path}" \
        "--markdown-out=${markdown_path}" \
        "--sha-out=${sha_path}" || return

    sha="$(cat "${sha_path}")"
    tags="$(jq -r '.tags | join(" ")' "${status_path}")"
    if [[ -z "${sha}" || "${sha}" == "null" ]]; then
        echo "FAIL: Failed to determine registry hash for ${workspace}" >&2
        return 1
    fi
    if [[ -n "${registry_hash}" && "${registry_hash}" != "${sha}" ]]; then
        echo "FAIL: Registry hash mismatch: ${workspace} has ${sha}, expected ${registry_hash}" >&2
        return 1
    fi
    registry_hash="${sha}"

    echo "${workspace}: $(cat "${markdown_path}")"
    if [[ -n "${tags}" ]]; then
        echo "${workspace}: registry commit ${sha} is tagged: ${tags}"
        return
    fi
    if [[ "${version}" == *-dev ]]; then
        echo "WARNING: ${workspace}: registry commit ${sha} is not a tagged version (ok for ${version})" >&2
        return
    fi
    echo "FAIL: ${workspace}: registry commit ${sha} is not a tagged version, required for release ${version}" >&2
    return 1
}

deps_report() {
    run_in_nested_mods _deps_report_mod
}

_deps_report_mod() {
    local module_dir="$1"
    local markdown_path
    local report_path
    local workspace

    workspace="$(workspace_name "$module_dir")"
    report_path="$(mktemp)"
    markdown_path="$(mktemp)"
    # shellcheck disable=SC2064
    trap "rm -f '${report_path}' '${markdown_path}'" RETURN

    workspace_bazel_run "$module_dir" update_module -- \
        --report \
        "--json-out=${report_path}" \
        "--markdown-out=${markdown_path}" || return

    echo "== ${workspace} =="
    cat "${markdown_path}"
    echo
}

deps_update() {
    run_in_nested_mods _deps_update_mod "$1"
    lockfiles_generate
}

_deps_update_mod() {
    local module_dir="$1"
    local dep="$2"
    local dep_name="${dep%%=*}"
    local output
    local status=0
    local workspace

    workspace="$(workspace_name "$module_dir")"
    output="$(workspace_bazel_run "$module_dir" update_module -- "${dep}" 2>&1)" || status=$?
    if [[ ${status} -eq 0 ]]; then
        printf '%s: %s\n' "${workspace}" "${output}"
        return
    fi
    if grep -q "Dependency ${dep_name} not found" <<< "${output}"; then
        echo "${workspace}: ${dep_name} not declared, skipping"
        return
    fi
    echo "${output}" >&2
    return "${status}"
}

registry_update() {
    local -a registry_args=()

    if [[ -n "${ENVOY_REGISTRY_HASH:-}" ]]; then
        registry_args+=("--@envoy_toolshed//dependency:registry_sha=${ENVOY_REGISTRY_HASH}")
    fi
    if [[ -n "${ENVOY_REGISTRY_ALLOW_UNSAFE:-}" ]]; then
        registry_args+=("--@envoy_toolshed//dependency:registry_allow_unsafe=true")
    fi

    run_in_registry_mods _registry_update_mod "${registry_args[@]}"
    registry_current_hash > /dev/null
    lockfiles_generate
}

_registry_update_mod() {
    local module_dir="$1"
    shift

    workspace_bazel_run "$module_dir" update_registry "$@"
}
