#!/bin/bash -eu
# Copyright 2018 Google Inc.
# Modifications 2019 fuzzit.dev Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

# xlarge resource_class.
# See note: https://circleci.com/docs/2.0/configuration-reference/#resource_class for why we
# hard code this (basically due to how docker works).
export NUM_CPUS=8

export CC=clang
export CXX=clang++

cat /proc/meminfo

# Disable UBSan vptr since target built with -fno-rtti.
export CFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link -fno-sanitize=vptr"
export CXXFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link -stdlib=libc++ -fno-sanitize=vptr"

declare -r FUZZER_TARGETS_CC=$(find . -name *_fuzz_test.cc)
declare -r FUZZER_TARGETS="$(for t in ${FUZZER_TARGETS_CC}; do echo "${t:2:-3}"; done)"

FUZZER_DICTIONARIES="\
"

# Copy $CFLAGS and $CXXFLAGS into Bazel command-line flags, for both
# compilation and linking.
#
# Some flags, such as `-stdlib=libc++`, generate warnings if used on a C source
# file. Since the build runs with `-Werror` this will cause it to break, so we
# use `--conlyopt` and `--cxxopt` instead of `--copt`.
#
declare -r EXTRA_BAZEL_FLAGS="$(
for f in ${CFLAGS}; do
  echo "--conlyopt=${f}" "--linkopt=${f}"
done
for f in ${CXXFLAGS}; do
  echo "--cxxopt=${f}" "--linkopt=${f}"
done
)"

declare BAZEL_BUILD_TARGETS=""
declare FILTERED_FUZZER_TARGETS=""
for t in ${FUZZER_TARGETS}
do
  declare BAZEL_PATH="//"$(dirname "$t")":"$(basename "$t")
  declare TAGGED=$(bazel query "attr('tags', 'no_fuzz', ${BAZEL_PATH})")
  if [ -z "${TAGGED}" ]
  then
    FILTERED_FUZZER_TARGETS+="$t "
    BAZEL_BUILD_TARGETS+="${BAZEL_PATH}_driverless "
  fi
done

# Build driverless libraries.
bazel build --jobs=${NUM_CPUS} --verbose_failures --dynamic_mode=off --spawn_strategy=standalone \
  --genrule_strategy=standalone --strip=never \
  --copt=-fno-sanitize=vptr --linkopt=-fno-sanitize=vptr \
  --define tcmalloc=disabled --define signal_trace=disabled \
  --define ENVOY_CONFIG_ASAN=1 --copt -D__SANITIZE_ADDRESS__ \
  --define force_libcpp=enabled --build_tag_filters=-no_asan \
  --linkopt=-lc++ --linkopt=-pthread \
  ${EXTRA_BAZEL_FLAGS} \
  ${BAZEL_BUILD_TARGETS[*]}

# Copy out test driverless binaries from bazel-bin/.
for t in ${FILTERED_FUZZER_TARGETS}
do
  TARGET_BASE="$(expr "$t" : '.*/\(.*\)_fuzz_test')"
  TARGET_DRIVERLESS=bazel-bin/"${t}"_driverless
  echo "Copying fuzzer $t"
  cp "${TARGET_DRIVERLESS}" /out/"${TARGET_BASE}"_fuzz_test
done

ls -l /out

# Uploading to Fuzzit

# This is is only used during the development of this pull-request and will be disabled after.
# This will use circle encrypted secret
export FUZZIT_API_KEY=${FUZZIT_API_KEY:-f8851643728efc52e9f031ca5bc7078123b06362739d55781aaefa290bcc5c580fa50d161649334f0f259bc8e16de431}
export FUZZING_TYPE=${1:-fuzzing}
export FUZZIT_ARGS="--type ${FUZZING_TYPE} --branch ${CIRCLE_BRANCH} --revision ${CIRCLE_SHA1}"

wget -O fuzzit https://github.com/fuzzitdev/fuzzit/releases/download/v1.2.5/fuzzit_1.2.5_Linux_x86_64
chmod a+x fuzzit
./fuzzit auth ${FUZZIT_API_KEY}
./fuzzit c job ${FUZZIT_ARGS} kZ716SPAxv25kvAcq6BR /out/access_log_formatter_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} HzjhLN0zDvCB0AcYjIWE /out/buffer_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} 795MFD6fEAUFxFkF2ME5 /out/codec_impl_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} yMNSqU1nsqIqHJTtK7t8 /out/config_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} kIhet6ukXfnov9RfHd5O /out/conn_manager_impl_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} R64kc25BLp5jVzJjbpis /out/h1_capture_direct_response_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} RFzPH3cMqGwJAJpzud9S /out/h1_capture_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} 2G7lSmSlNOSpAyf5I8M3 /out/header_map_impl_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} iev2vFIbZ8R7ADlxcExi /out/header_parser_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} 1mHRVaQ2owSTsmeZy0Ld /out/request_header_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} VskUn0xZX0DEWAB1bRUD /out/server_fuzz_test
./fuzzit c job ${FUZZIT_ARGS} K36Mzyn9jbKCubfFbslB /out/utility_fuzz_test
