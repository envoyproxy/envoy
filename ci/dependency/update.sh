#!/usr/bin/env bash

# Run a dependency update task in the build container and describe the result as JSON
# (`name`, `output`, `title`, `summary`) for the workflow to name the branch and PR.
#
#   update.sh bazel <name[=version]>... [version]   update the given modules
#   update.sh registry [<name[=version]>...|*] [commit]  bump the registry pin, then apply updates
#   update.sh lockfiles                             regenerate lockfiles

set -e -o pipefail

task="${1:-}"
dependency="${2:-}"
version="${3:-}"

read -r -a specs <<< "$dependency"

version_short="$version"
if [[ "$version" =~ ^[0-9a-fA-F]{40}$ ]]; then
    version_short="${version:0:7}"
fi
target="${version_short:-latest}"

fail() {
    echo "::error::$*" >&2
    exit 1
}

# Dependencies whose version changed in any MODULE.bazel, as `name version` lines.
changed_deps() {
    git diff -U0 -- ':(glob)**/MODULE.bazel' \
        | sed -n -E 's/^\+bazel_dep\(name = "([^"]+)", version = "([^"]+)".*/\1 \2/p' \
        | sort -u
}

module_diff_hash() {
    git diff -- ':(glob)**/MODULE.bazel' | sha256sum | cut -c1-7
}

case "$task" in
    bazel)
        (( ${#specs[@]} > 0 )) || fail 'dependency is required for the bazel task'
        if [[ -n "$version" ]]; then
            if (( ${#specs[@]} > 1 )) || [[ "${specs[0]}" == *=* ]]; then
                fail 'version can only be set for a single dependency without its own version'
            fi
            specs[0]="${specs[0]}=${version}"
        fi
        for spec in "${specs[@]}"; do
            [[ "$spec" != "*" ]] || fail '`*` is only supported by the registry task'
        done
        command='./ci/do_ci.sh deps.update'
        for spec in "${specs[@]}"; do
            printf -v spec '%q' "$spec"
            command+=" ${spec}"
        done
        ;;
    registry)
        if [[ " ${specs[*]} " == *" * "* && ${#specs[@]} -gt 1 ]]; then
            fail '`*` cannot be combined with other dependencies'
        fi
        command='./ci/do_ci.sh registry'
        for spec in "${specs[@]}"; do
            # The container shell would glob-expand a bare `*`, so pass it as `all`.
            [[ "$spec" == "*" ]] && spec=all
            printf -v spec '%q' "$spec"
            command+=" ${spec}"
        done
        ;;
    lockfiles)
        (( ${#specs[@]} == 0 )) || fail 'dependency is not supported by the lockfiles task'
        command='./ci/do_ci.sh lockfiles'
        ;;
    *)
        echo "Usage: $0 <bazel|registry|lockfiles> [dependency...] [version]" >&2
        exit 1
        ;;
esac

echo "Updating(${task}): ${specs[*]:-${task}} -> ${target}" >&2
./ci/run_envoy_docker.sh "$command" >&2

changed=()
while read -r line; do
    [[ -n "$line" ]] && changed+=("$line")
done < <(changed_deps)
changed_names=()
while read -r line; do
    [[ -n "$line" ]] && changed_names+=("$line")
done < <(printf '%s\n' "${changed[@]}" | cut -d' ' -f1 | sort -u)
summary=""
for line in "${changed[@]}"; do
    summary+="- \`${line% *}\` -> ${line#* }"$'\n'
done

case "$task" in
    bazel)
        (( ${#changed_names[@]} > 0 )) || fail "No dependency was updated"
        if (( ${#changed_names[@]} == 1 )); then
            name="${changed_names[0]}"
            output="${version_short:-${changed[0]#* }}"
            title="deps: Bump \`${name}\` -> ${output}"
        else
            name="$(IFS=+; echo "${changed_names[*]}")"
            output="$(module_diff_hash)"
            title="deps: Bump ${#changed_names[@]} deps"
        fi
        ;;
    registry)
        name=bazel-registry
        output="${version_short:-$(sed -n -E \
            's#^common --registry=https://raw\.githubusercontent\.com/envoyproxy/bazel-registry/([0-9a-f]+)$#\1#p' \
            .bazelrc | cut -c1-7)}"
        title="deps: Bump \`bazel-registry\` -> ${output}"
        if (( ${#changed_names[@]} == 1 )); then
            title+=" (+${changed_names[0]})"
        elif (( ${#changed_names[@]} > 1 )); then
            title+=" (+${#changed_names[@]} deps)"
        fi
        ;;
    lockfiles)
        name=lockfiles
        title="deps: Regenerate bazel lockfiles"
        lockfile_pathspecs=(':(glob)**/MODULE.bazel.lock' Cargo.lock Cargo.Bazel.envoy.lock)
        if [[ -z "$(git status --porcelain -- "${lockfile_pathspecs[@]}")" ]]; then
            echo 'Lockfiles are in sync, nothing to do' >&2
            output=in-sync
        else
            output=$(git diff -- "${lockfile_pathspecs[@]}" | sha256sum | cut -c1-7)
        fi
        ;;
esac

[[ -n "$output" ]] || fail "Unable to determine updated version for ${name}"

jq -n -c \
   --arg name "$name" \
   --arg output "$output" \
   --arg title "$title" \
   --arg summary "$summary" \
   '{name: $name, output: $output, title: $title, summary: $summary}'
