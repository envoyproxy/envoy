#!/usr/bin/env bash

version="1.8.0"
detekt_jar="detekt-cli-$version-all.jar"
wget -q "https://github.com/detekt/detekt/releases/download/v$version/$detekt_jar"

java -jar ${detekt_jar} --build-upon-default-config -c .kotlinlint.yml -i examples/kotlin/hello_world
