#!/bin/bash

TMP_OUTPUT_FILE="tmp.txt"
QUERY_OUTPUT_FILE="tmp_query.txt"
SEARCH_FOLDER="//source/common/..."
git fetch
git diff --name-only ..origin/master > TMP_OUTPUT_FILE

declare -A query_result

while IFS= read -r line
do
  #echo "$line"
  arrIN=(${line//// })
  #echo "$arrIN"
  if [[ "$arrIN" == "source" || "$arrIN" == "include" ]]; then
    bazel query "rdeps($SEARCH_FOLDER, $line, 1)" > QUERY_OUTPUT_FILE
    while IFS= read -r query_line
    do
      #echo "$query_line"
      query_result["$query_line"]="$query_line"
    done < QUERY_OUTPUT_FILE
  fi
done < TMP_OUTPUT_FILE

RES=""
for result in "${!query_result[@]}"; 
do 
  echo "$result"; 
  RES+="$result " 
done

export BUILD_TARGETS="$RES"
rm $TMP_OUTPUT_FILE
rm $QUERY_OUTPUT_FILE
