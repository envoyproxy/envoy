#!/bin/bash

set -euo pipefail

ktfmt_version="0.46"
readonly ktfmt_version

ktfmt_url="https://repo1.maven.org/maven2/com/facebook/ktfmt/${ktfmt_version}/ktfmt-${ktfmt_version}-jar-with-dependencies.jar"
readonly ktfmt_url

ktfmt_sha256="97fc7fbd194d01a9fa45d8147c0552403003d55bac4ab89d84d7bb4d5e3f48de"
readonly ktfmt_sha256

jdk_url="https://cdn.azul.com/zulu/bin/zulu11.68.17-ca-jdk11.0.21-linux_x64.tar.gz"
readonly jdk_url

jdk_sha256="725aba257da4bca14959060fea3faf59005eafdc2d5ccc3cb745403c5b60fb27"
readonly jdk_sha256

script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly script_root

ktfmt_jar="$script_root/tmp/ktfmt/versions/ktfmt-0.46.jar"
readonly ktfmt_jar

jdk="$script_root/tmp/jdk/versions/jdk11"
readonly jdk

java="${jdk}"/bin/java
readonly java

check_sha256sum() {
  sha256="$1"
  binary="$2"
  sha_check=$(echo "${sha256}" "${binary}" | sha256sum --quiet --check || true)
  echo "${sha_check}"
  if [[ -n "${sha_check}" ]]; then
    echo "Deleting ${binary}" >&2
    rm -f "$binary"
    exit 1
  fi
}

download_jdk() {
  mkdir -p "${jdk}"

  download_temp_dir=$(mktemp -d)
  jdk_tar_gz="${download_temp_dir}/jdk.tar.gz"
  curl --fail -L --retry 5 --retry-connrefused --silent --progress-bar \
    --output "${jdk_tar_gz}" "$jdk_url"

  check_sha256sum "${jdk_sha256}" "${jdk_tar_gz}"

  tar -C "${jdk}" -xf "${jdk_tar_gz}" --strip-components=1
}

download_ktfmt() {
  mkdir -p "$(dirname "${ktfmt_jar}")"

  curl --fail -L --retry 5 --retry-connrefused --silent --progress-bar \
    --output "$ktfmt_jar" "$ktfmt_url"

  check_sha256sum "${ktfmt_sha256}" "${ktfmt_jar}"
}

# TODO(fredyw): Use CI's JDK when available.
if [[ ! -f "${java}" ]]; then
  download_jdk
fi

if [[ ! -f "${ktfmt_jar}" ]]; then
  download_ktfmt
fi

"${java}" -jar "${ktfmt_jar}" --google-style "$@"
