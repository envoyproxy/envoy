#!/bin/bash

set -euo pipefail

ktfmt_version="0.46"
readonly ktfmt_version

ktfmt_url="https://repo1.maven.org/maven2/com/facebook/ktfmt/${ktfmt_version}/ktfmt-${ktfmt_version}-jar-with-dependencies.jar"
readonly ktfmt_url

ktfmt_sha256="97fc7fbd194d01a9fa45d8147c0552403003d55bac4ab89d84d7bb4d5e3f48de"
readonly ktfmt_sha256

script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly script_root

ktfmt_jar="$script_root/tmp/ktfmt/versions/ktfmt-0.46.jar"
readonly ktfmt_jar

download_ktfmt() {
  mkdir -p "$(dirname "${ktfmt_jar}")"

  curl --fail -L --retry 5 --retry-connrefused --silent --progress-bar \
    --output "$ktfmt_jar" "$ktfmt_url"
}

check_sha256sum() {
  sha_check=$(echo "$ktfmt_sha256 $ktfmt_jar" | sha256sum --quiet --check || true)
  echo "${sha_check}"
  if [[ -n "${sha_check}" ]]; then
    echo "Deleting ${ktfmt_jar}" >&2
    rm -f "$ktfmt_jar"
    exit 1
  fi
}

if [[ ! -f "${ktfmt_jar}" ]]; then
  download_ktfmt
  check_sha256sum
fi

java -jar "${ktfmt_jar}" --google-style "$@"
