#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

build_setup_args=""
if [[ "$1" == "fix_format" || "$1" == "check_format" || "$1" == "check_repositories" || \
        "$1" == "check_spelling" || "$1" == "fix_spelling" || "$1" == "bazel.clang_tidy" || \
        "$1" == "check_spelling_pedantic" || "$1" == "fix_spelling_pedantic" || "$1" == "bazel.compile_time_options" ]]; then
  build_setup_args="-nofetch"
fi

. "$(dirname "$0")"/setup_cache.sh
. "$(dirname "$0")"/build_setup.sh $build_setup_args
cd "${ENVOY_SRCDIR}"

echo "building using ${NUM_CPUS} CPUs"

function collect_build_profile() {
  cp -f "$(bazel info output_base)/command.profile" "${ENVOY_BUILD_PROFILE}/$1.profile" || true
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
  cp -f \
    "${ENVOY_SRCDIR}"/bazel-bin/source/exe/envoy-static \
    "${ENVOY_DELIVERY_DIR}"/"${DELIVERY_LOCATION}"
}

function cp_binary_for_image_build() {
  # TODO(mattklein123): Replace this with caching and a different job which creates images.
  echo "Copying binary for image build..."
  mkdir -p "${ENVOY_SRCDIR}"/build_"$1"
  cp -f "${ENVOY_DELIVERY_DIR}"/envoy "${ENVOY_SRCDIR}"/build_"$1"
  mkdir -p "${ENVOY_SRCDIR}"/build_"$1"_stripped
  strip "${ENVOY_DELIVERY_DIR}"/envoy -o "${ENVOY_SRCDIR}"/build_"$1"_stripped/envoy
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
  bazel build ${BAZEL_BUILD_OPTIONS} -c "${COMPILE_TYPE}" //source/exe:envoy-static ${CONFIG_ARGS}
  collect_build_profile "${BINARY_TYPE}"_build

  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  cp_binary_for_outside_access envoy

  cp_binary_for_image_build "${BINARY_TYPE}"
}

CI_TARGET=$1

if [[ $# -gt 1 ]]; then
  shift
  TEST_TARGETS=$*
else
  TEST_TARGETS=//test/...
fi

if [[ "$CI_TARGET" == "bazel.release" ]]; then
  # When testing memory consumption, we want to test against exact byte-counts
  # where possible. As these differ between platforms and compile options, we
  # define the 'release' builds as canonical and test them only in CI, so the
  # toolchain is kept consistent. This ifdef is checked in
  # test/common/stats/stat_test_utility.cc when computing
  # Stats::TestUtil::MemoryTest::mode().
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} --cxxopt=-DMEMORY_TEST_EXACT=1"

  setup_clang_toolchain
  echo "bazel release build with tests..."
  bazel_binary_build release

  echo "Testing ${TEST_TARGETS}"
  if [[ "$TEST_TARGETS" == "//test/..." ]]; then
    # We have various test binaries in the test directory such as tools, benchmarks, etc. We
    # run a build pass to make sure they compile.
    bazel build ${BAZEL_BUILD_OPTIONS} -c opt //include/... //source/... //test/...
  fi
  # Now run all of the tests which should already be compiled.
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
  echo "bazel ASAN/UBSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-asan ${TEST_TARGETS}
  echo "Building and testing envoy-filter-example tests..."
  pushd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-asan \
    //:echo2_integration_test //:envoy_binary_test
  popd
  # Also validate that integration test traffic tapping (useful when debugging etc.)
  # works. This requires that we set TAP_PATH. We do this under bazel.asan to
  # ensure a debug build in CI.
  echo "Validating integration test traffic tapping..."
  TAP_TMP=/tmp/tap/
  rm -rf "${TAP_TMP}"
  mkdir -p "${TAP_TMP}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-asan \
    //test/extensions/transport_sockets/tls/integration:ssl_integration_test \
    --test_env=TAP_PATH="${TAP_TMP}/tap"
  # Verify that some pb_text files have been created. We can't check for pcap,
  # since tcpdump is not available in general due to CircleCI lack of support
  # for privileged Docker executors.
  ls -l "${TAP_TMP}"/tap_*.pb_text > /dev/null
  exit 0
elif [[ "$CI_TARGET" == "bazel.tsan" ]]; then
  setup_clang_toolchain
  echo "bazel TSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-tsan ${TEST_TARGETS}
  echo "Building and testing envoy-filter-example tests..."
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c dbg --config=clang-tsan \
    //:echo2_integration_test //:envoy_binary_test
  exit 0
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
    --config libc++ \
    --define signal_trace=disabled \
    --define hot_restart=disabled \
    --define google_grpc=disabled \
    --define boringssl=fips \
    --define log_debug_assert_in_release=enabled \
    --define quiche=enabled \
    --define path_normalization_by_default=true \
  "
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel with different compiletime options build with tests..."
  # Building all the dependencies from scratch to link them against libc++.
  echo "Building..."
  bazel build ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c dbg //source/exe:envoy-static
  echo "Building and testing ${TEST_TARGETS}"
  bazel test ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c dbg ${TEST_TARGETS}

  # "--define log_debug_assert_in_release=enabled" must be tested with a release build, so run only
  # these tests under "-c opt" to save time in CI.
  bazel test ${BAZEL_BUILD_OPTIONS} ${COMPILE_TIME_OPTIONS} -c opt //test/common/common:assert_test //test/server:server_test
  exit 0
elif [[ "$CI_TARGET" == "bazel.ipv6_tests" ]]; then
  # This is around until Circle supports IPv6. We try to run a limited set of IPv6 tests as fast
  # as possible for basic sanity testing.

  # Hack to avoid returning IPv6 DNS
  sed -i 's_#precedence ::ffff:0:0/96  100_precedence ::ffff:0:0/96  100_' /etc/gai.conf
  # Debug IPv6 network issues
  apt-get update && apt-get install -y dnsutils net-tools curl && \
    ifconfig && \
    route -A inet -A inet6 && \
    curl -v https://go.googlesource.com && \
    curl -6 -v https://go.googlesource.com && \
    dig go.googlesource.com A go.googlesource.com AAAA

  setup_clang_toolchain
  echo "Testing..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} --test_env=ENVOY_IP_TEST_VERSIONS=v6only -c fastbuild \
    //test/integration/... //test/common/network/...
  exit 0
elif [[ "$CI_TARGET" == "bazel.api" ]]; then
  setup_clang_toolchain
  echo "Building API..."
  bazel build ${BAZEL_BUILD_OPTIONS} -c fastbuild @envoy_api//envoy/...
  echo "Testing API..."
  bazel_with_collection test ${BAZEL_BUILD_OPTIONS} -c fastbuild @envoy_api//test/... @envoy_api//tools/... \
    @envoy_api//tools:tap2pcap_test
  exit 0
elif [[ "$CI_TARGET" == "bazel.coverage" ]]; then
  setup_gcc_toolchain
  echo "bazel coverage build with tests ${TEST_TARGETS}"

  # gcovr is a pain to run with `bazel run`, so package it up into a
  # relocatable and hermetic-ish .par file.
  bazel build --python_version=PY2 @com_github_gcovr_gcovr//:gcovr.par
  export GCOVR="/tmp/gcovr.par"
  cp -f "${ENVOY_SRCDIR}/bazel-bin/external/com_github_gcovr_gcovr/gcovr.par" ${GCOVR}

  # Reduce the amount of memory and number of cores Bazel tries to use to
  # prevent it from launching too many subprocesses. This should prevent the
  # system from running out of memory and killing tasks. See discussion on
  # https://github.com/envoyproxy/envoy/pull/5611.
  # TODO(akonradi): use --local_cpu_resources flag once Bazel has a release
  # after 0.21.
  [ -z "$CIRCLECI" ] || export BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} --local_resources=12288,4,1"

  test/run_envoy_bazel_coverage.sh ${TEST_TARGETS}
  collect_build_profile coverage
  exit 0
elif [[ "$CI_TARGET" == "bazel.clang_tidy" ]]; then
  setup_clang_toolchain
  ci/run_clang_tidy.sh
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
    -c opt //source/exe:envoy-static
  # tar up the coverity results
  tar czvf "${ENVOY_BUILD_DIR}"/envoy-coverity-output.tgz -C "${ENVOY_BUILD_DIR}" cov-int
  # Copy the Coverity results somewhere that we can access outside of the container.
  cp -f \
     "${ENVOY_BUILD_DIR}"/envoy-coverity-output.tgz \
     "${ENVOY_DELIVERY_DIR}"/envoy-coverity-output.tgz
  exit 0
elif [[ "$CI_TARGET" == "fix_format" ]]; then
  echo "fix_format..."
  ./tools/check_format.py fix
  ./tools/format_python_tools.sh fix
  exit 0
elif [[ "$CI_TARGET" == "check_format" ]]; then
  echo "check_format_test..."
  ./tools/check_format_test_helper.py --log=WARN
  echo "check_format..."
  ./tools/check_format.py check
  ./tools/format_python_tools.sh check
  exit 0
elif [[ "$CI_TARGET" == "check_repositories" ]]; then
  echo "check_repositories..."
  ./tools/check_repositories.sh
  exit 0
elif [[ "$CI_TARGET" == "check_spelling" ]]; then
  echo "check_spelling..."
  ./tools/check_spelling.sh check
  exit 0
elif [[ "$CI_TARGET" == "fix_spelling" ]];then
  echo "fix_spell..."
  ./tools/check_spelling.sh fix
  exit 0
elif [[ "$CI_TARGET" == "check_spelling_pedantic" ]]; then
  echo "check_spelling_pedantic..."
  ./tools/check_spelling_pedantic.py check
  exit 0
elif [[ "$CI_TARGET" == "fix_spelling_pedantic" ]]; then
  echo "fix_spelling_pedantic..."
  ./tools/check_spelling_pedantic.py fix
  exit 0
elif [[ "$CI_TARGET" == "docs" ]]; then
  echo "generating docs..."
  docs/build.sh
  exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
