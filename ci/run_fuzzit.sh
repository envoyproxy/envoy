#!/bin/bash -eux

# helper function
function contains() {
    local n=$#
    local value=${!n}
    for ((i=1;i < $#;i++)) {
        if [ "${!i}" == "${value}" ]; then
            echo "y"
            return 0
        fi
    }
    echo "n"
    return 1
}

# Dynamically source fuzzing targets
declare -r FUZZER_TARGETS_CC=$(find . -name *_fuzz_test.cc)
declare -r FUZZER_TARGETS="$(for t in ${FUZZER_TARGETS_CC}; do echo "${t:2:-3}"; done)"

declare BAZEL_BUILD_TARGETS=""
for t in ${FUZZER_TARGETS}
do
  declare BAZEL_PATH="//"$(dirname "$t")":"$(basename "$t")
  declare TAGGED=$(bazel query "attr('tags', 'no_fuzz', ${BAZEL_PATH})")
  if [ -z "${TAGGED}" ]
  then
    FILTERED_FUZZER_TARGETS+="$t "
  fi
done


# run fuzzing regression or upload to Fuzzit for long running fuzzing job ($1 is either local-regression or fuzzing)
wget -O fuzzit https://github.com/fuzzitdev/fuzzit/releases/latest/download/fuzzit_Linux_x86_64
chmod a+x fuzzit

PREFIX=$(realpath /build/tmp/_bazel_bazel/*/execroot/envoy/bazel-out/k8-fastbuild/bin)
SLOW_TARGETS=("access-log-formatter" "h1-capture" "h1-capture-direct-response" "response-header" "request-header" "config" "server")
for t in ${FILTERED_FUZZER_TARGETS}
do
  TARGET_BASE="$(expr "$t" : '.*/\(.*\)_fuzz_test')"
  # Fuzzit target names can't contain underscore
  FUZZIT_TARGET_NAME=${TARGET_BASE//_/-}
  if [ $1 == "fuzzing" ]; then
    ./fuzzit create target --skip-if-exists --public-corpus envoyproxy/"${FUZZIT_TARGET_NAME}"
  fi
  # Skip slow targets for regression testing (this is still running in the cloud just won't run on Pull-Requests)
  if [ $(contains "${SLOW_TARGETS[@]}" "${FUZZIT_TARGET_NAME}") == "n" ]; then
    ./fuzzit create job --skip-if-not-exists --type $1 envoyproxy/"${FUZZIT_TARGET_NAME}" "${PREFIX}"/"${t}"_with_libfuzzer || exit 1
  fi
done
