#!/usr/bin/env bash

version="1.8.0"
detekt_jar="detekt-cli-$version-all.jar"
temp_dir="$(mktemp -d)"

echo "Downloading detekt..."
wget --directory-prefix ${temp_dir} \
  -q \
  --show-progress \
  "https://github.com/detekt/detekt/releases/download/v$version/$detekt_jar"

echo "Running linter..."
detekt_path="${temp_dir}/${detekt_jar}"
java -jar ${detekt_path} \
  --build-upon-default-config \
  -c .kotlinlint.yml \
  -i examples/kotlin,library/kotlin

rm -rf ${temp_dir}
echo "Done"
