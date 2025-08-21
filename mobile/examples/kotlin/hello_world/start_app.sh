#!/usr/bin/env bash

set -e

if [[ -z "${ANDROID_HOME}" ]]; then
  echo "ANDROID_HOME environment variable must be set."
  exit 1
fi

bazel build --config=mobile-release-android //examples/kotlin/hello_world:hello_envoy_kt

"${ANDROID_HOME}/platform-tools/adb" install -r --no-incremental bazel-bin/examples/kotlin/hello_world/hello_envoy_kt.apk
"${ANDROID_HOME}/platform-tools/adb" shell am start -n io.envoyproxy.envoymobile.helloenvoykotlin/.MainActivity
