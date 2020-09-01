#!/bin/bash -e

EXCLUDED_SHELLFILES=${EXCLUDED_SHELLFILES:-"^tools|^test|^examples|^ci|^bin|^source|^bazel|^.github"}


find_shell_files () {
    local shellfiles
    shellfiles=()
    shellfiles+=("$(git grep "^#!/bin/bash" | cut -d: -f1)")
    shellfiles+=("$(git grep "^#!/bin/sh" | cut -d: -f1)")
    shellfiles+=("$(find . -name "*.sh" | cut -d/ -f2-)")
    shellfiles=("$(echo "${shellfiles[@]}" | tr ' ' '\n' | sort | uniq)")
    for file in "${shellfiles[@]}"; do
	echo "$file"
    done
}

run_shellcheck_on () {
    local file
    file="$1"
    echo "Shellcheck: ${file}"
    # TODO: add -f diff when shellcheck version allows (ubuntu > bionic)
    shellcheck -x "$file"
}

run_shellchecks () {
    local all_shellfiles failed failure filtered_shellfiles skipped_count success_count
    failed=()
    readarray -t all_shellfiles <<< "$(find_shell_files)"
    readarray -t filtered_shellfiles <<< "$(find_shell_files | grep -vE "${EXCLUDED_SHELLFILES}")"

    for file in "${filtered_shellfiles[@]}"; do
	run_shellcheck_on "$file" || {
	    failed+=("$file")
	}
    done
    if [[ "${#failed[@]}" -ne 0 ]]; then
	echo -e "\nShellcheck failures:" >&2
	for failure in "${failed[@]}"; do
	    echo "$failure" >&2
	done
    fi
    skipped_count=$((${#all_shellfiles[@]} - ${#filtered_shellfiles[@]}))
    success_count=$((${#filtered_shellfiles[@]} - ${#failed[@]}))

    echo -e "\nShellcheck totals (skipped/failed/success): ${skipped_count}/${#failed[@]}/${success_count}"
    if [[ "${#failed[@]}" -ne 0 ]]; then
	return 1
    fi
}

run_shellchecks
