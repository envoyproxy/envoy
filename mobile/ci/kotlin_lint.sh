#!/usr/bin/env bash

version="1.0.0-RC15"
detekt_jar="detekt-cli-$version-all.jar"
wget -q "https://github.com/arturbosch/detekt/releases/download/$version/$detekt_jar"

java -jar ${detekt_jar} --build-upon-default-config -c .kotlinlint.yml -i examples/kotlin/hello_world
