#!/bin/bash -e

EXCLUDED_SHELLFILES=${EXCLUDED_SHELLFILES:-"^.github|.rst$|.md$"}


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
    local file op
    op="$1"
    file="$2"
    if [ "$op" != "fix" ]; then
	echo "Shellcheck: ${file}"
    fi
    shellcheck -x "$file"
}

run_shellchecks () {
    local all_shellfiles=() failed=() failure \
	  filtered_shellfiles=() found_shellfiles \
	  line skipped_count success_count

    found_shellfiles=$(find_shell_files)
    while read -r line; do all_shellfiles+=("$line"); done \
	<<< "$found_shellfiles"
    while read -r line; do filtered_shellfiles+=("$line"); done \
	<<< "$(echo -e "$found_shellfiles" | grep -vE "${EXCLUDED_SHELLFILES}")"

    for file in "${filtered_shellfiles[@]}"; do
	run_shellcheck_on "$1" "$file" || {
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

run_shellchecks "${1:-check}"
