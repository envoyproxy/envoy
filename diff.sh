#!/usr/bin/env bash

file="$(<$1)"

actual="$(echo "$file" | grep -m 1 Which | tail -n 1)"
expected="$(echo "$file" | grep -m 2 Which | tail -n 1)"

echo "$actual"
echo "$expected"

declare -i actual_char_count=$(echo "$actual" | wc -c)
declare -i expected_char_count=$(echo "$expected" | wc -c)
declare -i exit_code

if (( actual_char_count != expected_char_count )); then
  echo "actual word count: $actual_word_count expected_word_count: $expected_word_count"
  exit_code=1
fi

for ((i=0; i < actual_char_count; i++)) {
  #echo "${actual:$i:1} ${expected:$i:1}"
  if [[ ${actual:$i:1} != ${expected:$i:1} ]]; then
    echo "mismatch at index $i: actual ${actual:$i:30} | expected ${expected:$i:30}"
    exit_code=1
  fi
}

exit $exit_code
