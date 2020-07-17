#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

build_setup_args=""
if [[ "$1" == "fix_format" || "$1" == "check_format" || "$1" == "check_repositories" || \
        "$1" == "check_spelling" || "$1" == "fix_spelling" || "$1" == "bazel.clang_tidy" || \
        "$1" == "check_spelling_pedantic" || "$1" == "fix_spelling_pedantic" ]]; then
  build_setup_args="-nofetch"
fi

SRCDIR="${PWD}"
. "$(dirname "$0")"/setup_cache.sh
. "$(dirname "$0")"/build_setup.sh $build_setup_args
cd "${SRCDIR}"

echo "building using ${NUM_CPUS} CPUs"

function collect_build_profile() {
  cp -f "$(bazel info output_base)/command.profile.gz" "${ENVOY_BUILD_PROFILE}/$1.profile.gz" || true
}

function bazel_with_collection() {
  declare -r BAZEL_OUTPUT="${ENVOY_SRCDIR}"/bazel.output.txt
  bazel $* | tee "${BAZEL_OUTPUT}"
  declare BAZEL_STATUS="${PIPESTATUS[0]}"
  if [ "${BAZEL_STATUS}" != "0" ]
  then
    declare -r FAILED_TEST_LOGS="$(grep "  /build.*test.log" "${BAZEL_OUTPUT}" | sed -e 's/  \/build.*\/testlogs\/\(.*\)/\1/')"
    pushd bazel-testlogs
    for f in ${FAILED_TEST_LOGS}
    do
      cp --parents -f $f "${ENVOY_FAILED_TEST_LOGS}"
    done
    popd
    exit "${BAZEL_STATUS}"
  fi
  collect_build_profile $1
}

function cp_binary_for_outside_access() {
  DELIVERY_LOCATION="$1"
  ENVOY_BIN=$(echo "${ENVOY_BUILD_TARGET}" | sed -e 's#^@\([^/]*\)/#external/\1#;s#^//##;s#:#/#')
  cp -f \
    bazel-bin/"${ENVOY_BIN}" \
    "${ENVOY_DELIVERY_DIR}"/"${DELIVERY_LOCATION}"
}

function cp_binary_for_image_build() {
  # TODO(mattklein123): Replace this with caching and a different job which creates images.
  echo "Copying binary for image build..."
  mkdir -p "${ENVOY_SRCDIR}"/build_"$1"
  cp -f "${ENVOY_DELIVERY_DIR}"/envoy "${ENVOY_SRCDIR}"/build_"$1"
  mkdir -p "${ENVOY_SRCDIR}"/build_"$1"_stripped
  strip "${ENVOY_DELIVERY_DIR}"/envoy -o "${ENVOY_SRCDIR}"/build_"$1"_stripped/envoy

  # Copy for azp which doesn't preserve permissions, creating a tar archive
  tar czf "${ENVOY_BUILD_DIR}"/envoy_binary.tar.gz -C "${ENVOY_SRCDIR}" build_"$1" build_"$1"_stripped

  # Remove binaries to save space, only if BUILD_REASON exists (running in AZP)
  [[ -z "${BUILD_REASON}" ]] || \
    rm -rf "${ENVOY_SRCDIR}"/build_"$1" "${ENVOY_SRCDIR}"/build_"$1"_stripped "${ENVOY_DELIVERY_DIR}"/envoy \
      bazel-bin/"${ENVOY_BIN}"
}

function bazel_binary_build() {
  BINARY_TYPE="$1"
  if [[ "${BINARY_TYPE}" == "release" ]]; then
    COMPILE_TYPE="opt"
  elif [[ "${BINARY_TYPE}" == "debug" ]]; then
    COMPILE_TYPE="dbg"
  elif [[ "${BINARY_TYPE}" == "sizeopt" ]]; then
    # The COMPILE_TYPE variable is redundant in this case and is only here for
    # readability. It is already set in the .bazelrc config for sizeopt.
    COMPILE_TYPE="opt"
    CONFIG_ARGS="--config=sizeopt"
  elif [[ "${BINARY_TYPE}" == "fastbuild" ]]; then
    COMPILE_TYPE="fastbuild"
  fi

  echo "Building..."
  bazel build ${BAZEL_BUILD_OPTIONS} -c "${COMPILE_TYPE}" "${ENVOY_BUILD_TARGET}" ${CONFIG_ARGS}
  collect_build_profile "${BINARY_TYPE}"_build

  # Copy the built envoy binary somewhere that we can access outside of the
  # container.
  cp_binary_for_outside_access envoy

  cp_binary_for_image_build "${BINARY_TYPE}"
}

CI_TARGET=$1

if [[ $# -gt 1 ]]; then
  shift
  COVERAGE_TEST_TARGETS=$*
  TEST_TARGETS="$COVERAGE_TEST_TARGETS"
else
  # Coverage test will add QUICHE tests by itself.
  COVERAGE_TEST_TARGETS=//test/...
  TEST_TARGETS="${COVERAGE_TEST_TARGETS} @com_googlesource_quiche//:ci_tests"
fi

if [[ "$CI_TARGET" == "bazel.release" ]]; then
  # When testing memory consumption, we want to test against exact byte-counts
  # where possible. As these differ between platforms and compile options, we
  # define the 'release' builds as canonical and test them only in CI, so the
  # toolchain is kept consistent. This ifdef is checked in
  # test/common/stats/stat_test_utility.cc when computing
  # Stats::TestUtil::MemoryTest::mode().
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} --test_env=ENVOY_MEMORY_TEST_EXACT=true"

  setup_clang_toolchain
  echo "bazel release build with tests..."
  bazel_binary_build release

  echo "Testing ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c opt ${TEST_TARGETS}
  exit 0
elif [[ "$CI_TARGET" == "bazel.release.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel release build..."
  bazel_binary_build release
  exit 0
elif [[ "$CI_TARGET" == "bazel.sizeopt.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel size optimized build..."
  bazel_binary_build sizeopt
  exit 0
elif [[ "$CI_TARGET" == "bazel.sizeopt" ]]; then
  setup_clang_toolchain
  echo "bazel size optimized build with tests..."
  bazel_binary_build sizeopt
  echo "Testing ${TEST_TARGETS}"
  bazel test ${BAZEL_BUILD_OPTIONS} --config=sizeopt ${TEST_TARGETS}
  exit 0
elif [[ "$CI_TARGET" == "bazel.gcc" ]]; then
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} --test_env=HEAPCHECK="	
  setup_gcc_toolchain
  echo "bazel fastbuild build..."
  bazel_binary_build release

  echo "Testing ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c opt ${TEST_TARGETS}
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug" ]]; then
  setup_clang_toolchain
  echo "bazel debug build with tests..."
  bazel_binary_build debug
  echo "Testing ${TEST_TARGETS}"
  bazel test ${BAZEL_BUILD_OPTIONS} -c dbg ${TEST_TARGETS}
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel debug build..."
  bazel_binary_build debug
  exit 0
elif [[ "$CI_TARGET" == "bazel.asan" ]]; then
  setup_clang_toolchain
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-asan --build_tests_only"
  echo "bazel ASAN/UBSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} ${TEST_TARGETS}
  if [ "${ENVOY_BUILD_FILTER_EXAMPLE}" == "1" ]; then
    echo "Building and testing envoy-filter-example tests..."
    pushd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
    bazel_with_collection test ${BAZEL_BUILD_OPTIONS} ${ENVOY_FILTER_EXAMPLE_TESTS}
    popd
  fi
  # Also validate that integration test traffic tapping (useful when debugging etc.)
  # works. This requires that we set TAP_PATH. We do this under bazel.asan to
  # ensure a debug build in CI.
  echo "Validating integration test traffic tapping..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} \
    --run_under=@envoy//bazel/test:verify_tap_test.sh \
    //test/extensions/transport_sockets/tls/integration:ssl_integration_test
  exit 0
elif [[ "$CI_TARGET" == "bazel.tsan" ]]; then
  setup_clang_toolchain
  echo "bazel TSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-tsan --build_tests_only ${TEST_TARGETS}
  if [ "${ENVOY_BUILD_FILTER_EXAMPLE}" == "1" ]; then
    echo "Building and testing envoy-filter-example tests..."
    pushd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
    bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-tsan ${ENVOY_FILTER_EXAMPLE_TESTS}
    popd
  fi
  exit 0
elif [[ "$CI_TARGET" == "bazel.msan" ]]; then
  ENVOY_STDLIB=libc++
  setup_clang_toolchain
  # rbe-toolchain-msan must comes as first to win library link order.
  BAZEL_BUILD_OPTIONS="--config=rbe-toolchain-msan ${BAZEL_BUILD_OPTIONS} -c dbg --build_tests_only"
  echo "bazel MSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} ${TEST_TARGETS}
elif [[ "$CI_TARGET" == "bazel.dev" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with tests..."
  echo "Building..."
  bazel_binary_build fastbuild

  echo "Building and testing ${TEST_TARGETS}"
  bazel test ${BAZEL_BUILD_OPTIONS} -c fastbuild ${TEST_TARGETS}
  exit 0
elif [[ "$CI_TARGET" == "bazel.compile_time_options" ]]; then
  # Right now, none of the available compile-time options conflict with each other. If this
  # changes, this build type may need to be broken up.
  # TODO(mpwarres): remove quiche=enabled once QUICHE is built by default.
  COMPILE_TIME_OPTIONS="\
    --define signal_trace=disabled \
    --define hot_restart=disabled \
    --define google_grpc=disabled \
    --define boringssl=fips \
    --define log_debug_assert_in_release=enabled \
    --define quiche=enabled \
    --define path_normalization_by_default=true \
    --define deprecated_features=disabled \
    --define --cxxopt=-std=c++14 \
  "
  ENVOY_STDLIB="${ENVOY_STDLIB:-libstdc++}"
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel with different compiletime options build with tests..."

  if [[ "${TEST_TARGETS}" == "//test/..." ]]; then
    cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
    TEST_TARGETS="@envoy//test/..."
  fi
  # Building all the dependencies from scratch to link them against libc++.
  echo "Building..."
  bazel build ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c dbg @envoy//source/exe:envoy-static --build_tag_filters=-nofips
  echo "Building and testing ${TEST_TARGETS}"
  bazel test ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c dbg ${TEST_TARGETS} --test_tag_filters=-nofips --build_tests_only

  # "--define log_debug_assert_in_release=enabled" must be tested with a release build, so run only
  # these tests under "-c opt" to save time in CI.
  bazel test ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c opt @envoy//test/common/common:assert_test @envoy//test/server:server_test

  exit 0
elif [[ "$CI_TARGET" == "bazel.api" ]]; then
  setup_clang_toolchain
  echo "Validating API structure..."
  ./tools/api/validate_structure.py
  echo "Building API..."
  bazel build ${BAZEL_BUILD_OPTIONS} -c fastbuild @envoy_api_canonical//envoy/...
  echo "Testing API..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c fastbuild @envoy_api_canonical//test/... @envoy_api_canonical//tools/... \
    @envoy_api_canonical//tools:tap2pcap_test
  echo "Testing API boosting (unit tests)..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c fastbuild @envoy_dev//clang_tools/api_booster/...
  echo "Testing API boosting (golden C++ tests)..."
  # We use custom BAZEL_BUILD_OPTIONS here; the API booster isn't capable of working with libc++ yet.
  LLVM_CONFIG="${LLVM_ROOT}"/bin/llvm-config BAZEL_BUILD_OPTIONS="--config=clang" python3.8 ./tools/api_boost/api_boost_test.py
  exit 0
elif [[ "$CI_TARGET" == "bazel.coverage" ]]; then
  setup_clang_toolchain
  echo "bazel coverage build with tests ${COVERAGE_TEST_TARGETS}"

  # Reduce the amount of memory Bazel tries to use to prevent it from launching too many subprocesses.
  # This should prevent the system from running out of memory and killing tasks. See discussion on
  # https://github.com/envoyproxy/envoy/pull/5611.
  [ -z "$CIRCLECI" ] || export BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} --local_ram_resources=12288"

  test/run_envoy_bazel_coverage.sh ${COVERAGE_TEST_TARGETS}
  collect_build_profile coverage
  exit 0
elif [[ "$CI_TARGET" == "bazel.clang_tidy" ]]; then
  setup_clang_toolchain
  NUM_CPUS=$NUM_CPUS ci/run_clang_tidy.sh
  exit 0
elif [[ "$CI_TARGET" == "bazel.coverity" ]]; then
  # Coverity Scan version 2017.07 fails to analyze the entirely of the Envoy
  # build when compiled with Clang 5. Revisit when Coverity Scan explicitly
  # supports Clang 5. Until this issue is resolved, run Coverity Scan with
  # the GCC toolchain.
  setup_gcc_toolchain
  echo "bazel Coverity Scan build"
  echo "Building..."
  /build/cov-analysis/bin/cov-build --dir "${ENVOY_BUILD_DIR}"/cov-int bazel build --action_env=LD_PRELOAD ${BAZEL_BUILD_OPTIONS} \
    -c opt "${ENVOY_BUILD_TARGET}"
  # tar up the coverity results
  tar czvf "${ENVOY_BUILD_DIR}"/envoy-coverity-output.tgz -C "${ENVOY_BUILD_DIR}" cov-int
  # Copy the Coverity results somewhere that we can access outside of the container.
  cp -f \
     "${ENVOY_BUILD_DIR}"/envoy-coverity-output.tgz \
     "${ENVOY_DELIVERY_DIR}"/envoy-coverity-output.tgz
  exit 0
elif [[ "$CI_TARGET" == "bazel.fuzz" ]]; then
  setup_clang_toolchain
  FUZZ_TEST_TARGETS="$(bazel query "attr('tags','fuzzer',${TEST_TARGETS})")"
  echo "bazel ASAN libFuzzer build with fuzz tests ${FUZZ_TEST_TARGETS}"
  echo "Building envoy fuzzers and executing 100 fuzz iterations..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} --config=asan-fuzzer ${FUZZ_TEST_TARGETS} --test_arg="-runs=10"
  exit 0
elif [[ "$CI_TARGET" == "fix_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain
  echo "fix_format..."
  ./tools/code_format/check_format.py fix
  ./tools/code_format/format_python_tools.sh fix
  ./tools/proto_format/proto_format.sh fix --test
  exit 0
elif [[ "$CI_TARGET" == "check_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain
  echo "check_format_test..."
  ./tools/code_format/check_format_test_helper.sh --log=WARN
  echo "check_format..."
  ./tools/code_format/check_format.py check
  ./tools/code_format/format_python_tools.sh check
  ./tools/proto_format/proto_format.sh check --test
  exit 0
elif [[ "$CI_TARGET" == "check_repositories" ]]; then
  echo "check_repositories..."
  ./tools/check_repositories.sh
  exit 0
elif [[ "$CI_TARGET" == "check_spelling" ]]; then
  echo "check_spelling..."
  ./tools/spelling/check_spelling.sh check
  exit 0
elif [[ "$CI_TARGET" == "fix_spelling" ]];then
  echo "fix_spell..."
  ./tools/spelling/check_spelling.sh fix
  exit 0
elif [[ "$CI_TARGET" == "check_spelling_pedantic" ]]; then
  echo "check_spelling_pedantic..."
  ./tools/spelling/check_spelling_pedantic.py --mark check
  exit 0
elif [[ "$CI_TARGET" == "fix_spelling_pedantic" ]]; then
  echo "fix_spelling_pedantic..."
  ./tools/spelling/check_spelling_pedantic.py fix
  exit 0
elif [[ "$CI_TARGET" == "docs" ]]; then
  echo "generating docs..."
  docs/build.sh
  exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
