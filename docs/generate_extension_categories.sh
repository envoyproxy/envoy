#!/bin/bash
#!/usr/bin/env bash

set -e

output_file_name="$1"/api/factory_categories.csv

echo Writing into $output_file_name

echo "\"Category Name\", \"Source File\"" > $output_file_name

find . -name *.h -exec grep -HF "std::string category()" {} \; | while IFS= read -r line
do
  tokens=( $line )
  category=${tokens[6]}
  category=${category%;*}
  filepath=${tokens[0]}
  filepath=${filepath%:*}
  filepath=${filepath#*.}
  echo "$category, \"https://github.com/envoyproxy/envoy/blob/master$filepath\"" >> $output_file_name
done
