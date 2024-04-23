#!/usr/bin/env bash

file_1="$(<$1)"
file_2="$(<$2)"

declare -i word_count_1=$(echo "$file_1" | wc -c)
declare -i word_count_2=$(echo "$file_2" | wc -c)
declare -i exit_code

if (( word_count_1 != word_count_2 )); then
  echo "word_count_1: $word_count_1 word_count_2: $word_count_2" >&2
  exit_code=1
fi

for ((i=0; i < word_count_1; i++)) {
  if [[ ${file_1:$i:1} != ${file_2:$i:1} ]]; then
    echo "mismatch at index $i: first ${file_1:$i:30} |  second ${file_2:$i:30}" >&2
    exit_code=1
		exit 11
  fi
}

exit $exit_code
