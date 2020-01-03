#!/usr/bin/env bash

set -e

output_file_name="$1"/api/factory_categories.csv

echo Writing into "$output_file_name"

echo "\"Category Name\", \"Source File\"" > "$output_file_name"

find . -name *.h -exec grep -HP "static\s+const\s+char\s+FACTORY_CATEGORY\[\s*\]\s*=\s*\".*\"\s*;" {} \; | while IFS= read -r line
do
  # Split the file path to the left of the ":"
  IFS=':' tokens=( $line )
  filepath=${tokens[0]}
  # Remove leading "."
  filepath=${filepath#*.}

  # Grab the string between quotes on the right side of ":"
  IFS='"' code_tokens=( ${tokens[1]} )
  category=${code_tokens[1]}
  echo "\"$category\", \"https://github.com/envoyproxy/envoy/blob/master$filepath\"" >> "$output_file_name"
done
