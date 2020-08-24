#!/bin/bash

TMP_OUTPUT_FILE="tmp.txt"
QUERY_OUTPUT_FILE="tmp_query.txt"

# This limits the directory that bazel query is going to search under.
SEARCH_FOLDER="//source/common/..."
git fetch
git diff --name-only ..origin/master > $TMP_OUTPUT_FILE

declare -A query_result

while IFS= read -r line
do
  # Split the line by '/'.
  splited_line=(${line//// })
  # Only targets under those folders.
  if [[ "$splited_line" == "source" || "$splited_line" == "include" ]]; then
    bazel query "rdeps($SEARCH_FOLDER, $line, 1)" > $QUERY_OUTPUT_FILE
    while IFS= read -r query_line
    do
      query_result["$query_line"]="$query_line"
    done < $QUERY_OUTPUT_FILE
  fi
done < $TMP_OUTPUT_FILE

RES=""
for result in "${!query_result[@]}"; 
do 
  RES+="$result " 
done

export BUILD_TARGETS="$RES"
rm $TMP_OUTPUT_FILE
rm $QUERY_OUTPUT_FILE
