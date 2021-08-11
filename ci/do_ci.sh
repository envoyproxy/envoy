#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e


build_setup_args=""
if [[ "$1" == "format_pre" || "$1" == "fix_format" || "$1" == "check_format" || "$1" == "docs" ||  \
          "$1" == "bazel.clang_tidy" || "$1" == "tooling" || "$1" == "deps" || "$1" == "verify_examples" || \
          "$1" == "verify_build_examples" ]]; then
    build_setup_args="-nofetch"
fi

# TODO(phlax): Clarify and/or integrate SRCDIR and ENVOY_SRCDIR
export SRCDIR="${SRCDIR:-$PWD}"
export ENVOY_SRCDIR="${ENVOY_SRCDIR:-$PWD}"
NO_BUILD_SETUP="${NO_BUILD_SETUP:-}"

if [[ -z "$NO_BUILD_SETUP" ]]; then
    # shellcheck source=ci/setup_cache.sh
    . "$(dirname "$0")"/setup_cache.sh
    # shellcheck source=ci/build_setup.sh
    . "$(dirname "$0")"/build_setup.sh $build_setup_args
fi
cd "${SRCDIR}"

if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
  BUILD_ARCH_DIR="/linux/amd64"
elif [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
  BUILD_ARCH_DIR="/linux/arm64"
else
  # Fall back to use the ENVOY_BUILD_ARCH itself.
  BUILD_ARCH_DIR="/linux/${ENVOY_BUILD_ARCH}"
fi

echo "building using ${NUM_CPUS} CPUs"
echo "building for ${ENVOY_BUILD_ARCH}"

function collect_build_profile() {
  declare -g build_profile_count=${build_profile_count:-1}
  mv -f "$(bazel info output_base)/command.profile.gz" "${ENVOY_BUILD_PROFILE}/${build_profile_count}-$1.profile.gz" || true
  ((build_profile_count++))
}

function bazel_with_collection() {
  local failed_logs
  declare -r BAZEL_OUTPUT="${ENVOY_SRCDIR}"/bazel.output.txt
  bazel "$@" | tee "${BAZEL_OUTPUT}"
  declare BAZEL_STATUS="${PIPESTATUS[0]}"
  if [ "${BAZEL_STATUS}" != "0" ]
  then
    pushd bazel-testlogs
    failed_logs=$(grep "  /build.*test.log" "${BAZEL_OUTPUT}" | sed -e 's/  \/build.*\/testlogs\/\(.*\)/\1/')
    while read -r f; do
      cp --parents -f "$f" "${ENVOY_FAILED_TEST_LOGS}"
    done <<< "$failed_logs"
    popd
    exit "${BAZEL_STATUS}"
  fi
  collect_build_profile "$1"
  run_process_test_result
}

function cp_binary_for_outside_access() {
  DELIVERY_LOCATION="$1"
  cp -f \
    bazel-bin/"${ENVOY_BIN}" \
    "${ENVOY_DELIVERY_DIR}"/"${DELIVERY_LOCATION}"
}

function cp_debug_info_for_outside_access() {
  DELIVERY_LOCATION="$1"
  cp -f \
    bazel-bin/"${ENVOY_BIN}".dwp \
    "${ENVOY_DELIVERY_DIR}"/"${DELIVERY_LOCATION}".dwp
}


function cp_binary_for_image_build() {
  # TODO(mattklein123): Replace this with caching and a different job which creates images.
  local BASE_TARGET_DIR="${ENVOY_SRCDIR}${BUILD_ARCH_DIR}"
  echo "Copying binary for image build..."
  COMPILE_TYPE="$2"
  mkdir -p "${BASE_TARGET_DIR}"/build_"$1"
  cp -f "${ENVOY_DELIVERY_DIR}"/envoy "${BASE_TARGET_DIR}"/build_"$1"
  # Copy the su-exec utility binary into the image
  cp -f bazel-bin/external/com_github_ncopa_suexec/su-exec "${BASE_TARGET_DIR}"/build_"$1"
  if [[ "${COMPILE_TYPE}" == "dbg" || "${COMPILE_TYPE}" == "opt" ]]; then
    cp -f "${ENVOY_DELIVERY_DIR}"/envoy.dwp "${BASE_TARGET_DIR}"/build_"$1"
  fi
  mkdir -p "${BASE_TARGET_DIR}"/build_"$1"_stripped
  strip "${ENVOY_DELIVERY_DIR}"/envoy -o "${BASE_TARGET_DIR}"/build_"$1"_stripped/envoy

  # Copy for azp which doesn't preserve permissions, creating a tar archive
  tar czf "${ENVOY_BUILD_DIR}"/envoy_binary.tar.gz -C "${BASE_TARGET_DIR}" build_"$1" build_"$1"_stripped

  # Remove binaries to save space, only if BUILD_REASON exists (running in AZP)
  [[ -z "${BUILD_REASON}" ]] || \
    rm -rf "${BASE_TARGET_DIR}"/build_"$1" "${BASE_TARGET_DIR}"/build_"$1"_stripped "${ENVOY_DELIVERY_DIR}"/envoy{,.dwp} \
      bazel-bin/"${ENVOY_BIN}"{,.dwp}
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
  ENVOY_BIN=$(echo "${ENVOY_BUILD_TARGET}" | sed -e 's#^@\([^/]*\)/#external/\1#;s#^//##;s#:#/#')

  # This is a workaround for https://github.com/bazelbuild/bazel/issues/11834
  [[ -n "${ENVOY_RBE}" ]] && rm -rf bazel-bin/"${ENVOY_BIN}"*

  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" "${ENVOY_BUILD_TARGET}" ${CONFIG_ARGS}
  collect_build_profile "${BINARY_TYPE}"_build

  # Copy the built envoy binary somewhere that we can access outside of the
  # container.
  cp_binary_for_outside_access envoy

  if [[ "${COMPILE_TYPE}" == "dbg" || "${COMPILE_TYPE}" == "opt" ]]; then
    # Generate dwp file for debugging since we used split DWARF to reduce binary
    # size
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" "${ENVOY_BUILD_DEBUG_INFORMATION}" ${CONFIG_ARGS}
    # Copy the debug information
    cp_debug_info_for_outside_access envoy
  fi

  # Build su-exec utility
  bazel build external:su-exec
  cp_binary_for_image_build "${BINARY_TYPE}" "${COMPILE_TYPE}"

}

function run_process_test_result() {
  if [[ -z "$CI_SKIP_PROCESS_TEST_RESULTS" ]] && [[ $(find "$TEST_TMPDIR" -name "*_attempt.xml" 2> /dev/null) ]]; then
      echo "running flaky test reporting script"
      "${ENVOY_SRCDIR}"/ci/flaky_test/run_process_xml.sh "$CI_TARGET"
  else
      echo "no flaky test results found"
  fi
}

function run_ci_verify () {
  echo "verify examples..."
  docker load < "$ENVOY_DOCKER_BUILD_DIR/docker/envoy-docker-images.tar.xz"
  _images=$(docker image list --format "{{.Repository}}")
  while read -r line; do images+=("$line"); done \
      <<< "$_images"
  _tags=$(docker image list --format "{{.Tag}}")
  while read -r line; do tags+=("$line"); done \
      <<< "$_tags"
  for i in "${!images[@]}"; do
      if [[ "${images[i]}" =~ "envoy" ]]; then
          docker tag "${images[$i]}:${tags[$i]}" "${images[$i]}:latest"
      fi
  done
  docker images
  sudo apt-get update -y
  sudo apt-get install -y -qq --no-install-recommends expect redis-tools
  export DOCKER_NO_PULL=1
  umask 027
  chmod -R o-rwx examples/
  "${ENVOY_SRCDIR}"/ci/verify_examples.sh "${@}" || exit
}

CI_TARGET=$1
shift

if [[ $# -ge 1 ]]; then
  COVERAGE_TEST_TARGETS=("$@")
  TEST_TARGETS=("$@")
else
  # Coverage test will add QUICHE tests by itself.
  COVERAGE_TEST_TARGETS=("//test/...")
  TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "@com_googlesource_quiche//:ci_tests")
fi

if [[ "$CI_TARGET" == "bazel.release" ]]; then
  # When testing memory consumption, we want to test against exact byte-counts
  # where possible. As these differ between platforms and compile options, we
  # define the 'release' builds as canonical and test them only in CI, so the
  # toolchain is kept consistent. This ifdef is checked in
  # test/common/stats/stat_test_utility.cc when computing
  # Stats::TestUtil::MemoryTest::mode().
  [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]] && BAZEL_BUILD_OPTIONS+=("--test_env=ENVOY_MEMORY_TEST_EXACT=true")

  setup_clang_toolchain
  echo "Testing ${TEST_TARGETS[*]} with options: ${BAZEL_BUILD_OPTIONS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c opt "${TEST_TARGETS[@]}"

  echo "bazel release build with tests..."
  bazel_binary_build release
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
  echo "Testing ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --config=sizeopt "${TEST_TARGETS[@]}"

  echo "bazel size optimized build with tests..."
  bazel_binary_build sizeopt
  exit 0
elif [[ "$CI_TARGET" == "bazel.gcc" ]]; then
  BAZEL_BUILD_OPTIONS+=("--test_env=HEAPCHECK=")
  setup_gcc_toolchain

  echo "Testing ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild -- "${TEST_TARGETS[@]}"

  echo "bazel release build with gcc..."
  bazel_binary_build fastbuild
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug" ]]; then
  setup_clang_toolchain
  echo "Testing ${TEST_TARGETS[*]}"
  bazel test "${BAZEL_BUILD_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}"

  echo "bazel debug build with tests..."
  bazel_binary_build debug
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel debug build..."
  bazel_binary_build debug
  exit 0
elif [[ "$CI_TARGET" == "bazel.asan" ]]; then
  setup_clang_toolchain
  BAZEL_BUILD_OPTIONS+=(-c dbg "--config=clang-asan" "--build_tests_only")
  echo "bazel ASAN/UBSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" "${TEST_TARGETS[@]}"
  if [ "${ENVOY_BUILD_FILTER_EXAMPLE}" == "1" ]; then
    echo "Building and testing envoy-filter-example tests..."
    pushd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
    bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" "${ENVOY_FILTER_EXAMPLE_TESTS[@]}"
    popd
  fi

  # TODO(mattklein123): This part of the test is now flaky in CI and it's unclear why, possibly
  # due to sandboxing issue. Debug and enable it again.
  # if [ "${CI_SKIP_INTEGRATION_TEST_TRAFFIC_TAPPING}" != "1" ] ; then
    # Also validate that integration test traffic tapping (useful when debugging etc.)
    # works. This requires that we set TAP_PATH. We do this under bazel.asan to
    # ensure a debug build in CI.
    # echo "Validating integration test traffic tapping..."
    # bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" \
    #   --run_under=@envoy//bazel/test:verify_tap_test.sh \
    #   //test/extensions/transport_sockets/tls/integration:ssl_integration_test
  # fi
  exit 0
elif [[ "$CI_TARGET" == "bazel.tsan" ]]; then
  setup_clang_toolchain
  echo "bazel TSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
  bazel_with_collection test --config=rbe-toolchain-tsan "${BAZEL_BUILD_OPTIONS[@]}" -c dbg --build_tests_only "${TEST_TARGETS[@]}"
  if [ "${ENVOY_BUILD_FILTER_EXAMPLE}" == "1" ]; then
    echo "Building and testing envoy-filter-example tests..."
    pushd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
    bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c dbg --config=clang-tsan "${ENVOY_FILTER_EXAMPLE_TESTS[@]}"
    popd
  fi
  exit 0
elif [[ "$CI_TARGET" == "bazel.msan" ]]; then
  ENVOY_STDLIB=libc++
  setup_clang_toolchain
  # rbe-toolchain-msan must comes as first to win library link order.
  BAZEL_BUILD_OPTIONS=("--config=rbe-toolchain-msan" "${BAZEL_BUILD_OPTIONS[@]}" "-c" "dbg" "--build_tests_only")
  echo "bazel MSAN debug build with tests"
  echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" "${TEST_TARGETS[@]}"
  exit 0
elif [[ "$CI_TARGET" == "bazel.dev" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with tests..."
  echo "Building..."
  bazel_binary_build fastbuild

  echo "Testing ${TEST_TARGETS[*]}"
  bazel test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild "${TEST_TARGETS[@]}"
  exit 0
elif [[ "$CI_TARGET" == "bazel.compile_time_options" ]]; then
  # Right now, none of the available compile-time options conflict with each other. If this
  # changes, this build type may need to be broken up.
  COMPILE_TIME_OPTIONS=(
    "--define" "signal_trace=disabled"
    "--define" "hot_restart=disabled"
    "--define" "google_grpc=disabled"
    "--define" "boringssl=fips"
    "--define" "log_debug_assert_in_release=enabled"
    "--define" "path_normalization_by_default=true"
    "--define" "deprecated_features=disabled"
    "--define" "tcmalloc=gperftools"
    "--define" "zlib=ng"
    "--@envoy//bazel:http3=False"
    "--@envoy//source/extensions/filters/http/kill_request:enabled"
    "--test_env=ENVOY_HAS_EXTRA_EXTENSIONS=true")

  ENVOY_STDLIB="${ENVOY_STDLIB:-libstdc++}"
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel with different compiletime options build with tests..."

  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  TEST_TARGETS=("${TEST_TARGETS[@]/#\/\//@envoy\/\/}")

  # Building all the dependencies from scratch to link them against libc++.
  echo "Building and testing with wasm=wamr: ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wamr "${COMPILE_TIME_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}" --test_tag_filters=-nofips --build_tests_only

  echo "Building and testing with wasm=wasmtime: ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wasmtime "${COMPILE_TIME_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}" --test_tag_filters=-nofips --build_tests_only

  echo "Building and testing with wasm=wavm: ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wavm "${COMPILE_TIME_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}" --test_tag_filters=-nofips --build_tests_only

  # "--define log_debug_assert_in_release=enabled" must be tested with a release build, so run only
  # these tests under "-c opt" to save time in CI.
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wavm "${COMPILE_TIME_OPTIONS[@]}" -c opt @envoy//test/common/common:assert_test @envoy//test/server:server_test

  # "--define log_fast_debug_assert_in_release=enabled" must be tested with a release build, so run only these tests under "-c opt" to save time in CI. This option will test only ASSERT()s without SLOW_ASSERT()s, so additionally disable "--define log_debug_assert_in_release" which compiles in both.
    bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wavm "${COMPILE_TIME_OPTIONS[@]}" -c opt @envoy//test/common/common:assert_test --define log_fast_debug_assert_in_release=enabled --define log_debug_assert_in_release=disabled

  echo "Building binary with wasm=wavm..."
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wavm "${COMPILE_TIME_OPTIONS[@]}" -c dbg @envoy//source/exe:envoy-static --build_tag_filters=-nofips
  collect_build_profile build
  exit 0
elif [[ "$CI_TARGET" == "bazel.api" ]]; then
  # Use libstdc++ because the API booster links to prebuilt libclang*/libLLVM* installed in /opt/llvm/lib,
  # which is built with libstdc++. Using libstdc++ for whole of the API CI job to avoid unnecessary rebuild.
  ENVOY_STDLIB="libstdc++"
  setup_clang_toolchain
  export LLVM_CONFIG="${LLVM_ROOT}"/bin/llvm-config
  echo "Validating API structure..."
  "${ENVOY_SRCDIR}"/tools/api/validate_structure.py
  echo "Testing API and API Boosting..."
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild @envoy_api_canonical//test/... @envoy_api_canonical//tools/... \
    @envoy_api_canonical//tools:tap2pcap_test @envoy_dev//clang_tools/api_booster/...
  echo "Building API..."
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild @envoy_api_canonical//envoy/...
  echo "Testing API boosting (golden C++ tests)..."
  # We use custom BAZEL_BUILD_OPTIONS here; the API booster isn't capable of working with libc++ yet.
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" python3.8 "${ENVOY_SRCDIR}"/tools/api_boost/api_boost_test.py
  exit 0
elif [[ "$CI_TARGET" == "bazel.coverage" || "$CI_TARGET" == "bazel.fuzz_coverage" ]]; then
  setup_clang_toolchain
  echo "${CI_TARGET} build with tests ${COVERAGE_TEST_TARGETS[*]}"

  [[ "$CI_TARGET" == "bazel.fuzz_coverage" ]] && export FUZZ_COVERAGE=true

  # We use custom BAZEL_BUILD_OPTIONS here to cover profiler's code.
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]} --define tcmalloc=gperftools" "${ENVOY_SRCDIR}"/test/run_envoy_bazel_coverage.sh "${COVERAGE_TEST_TARGETS[@]}"
  collect_build_profile coverage
  exit 0
elif [[ "$CI_TARGET" == "bazel.clang_tidy" ]]; then
  # clang-tidy will warn on standard library issues with libc++
  ENVOY_STDLIB="libstdc++"
  setup_clang_toolchain
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" NUM_CPUS=$NUM_CPUS "${ENVOY_SRCDIR}"/ci/run_clang_tidy.sh "$@"
  exit 0
elif [[ "$CI_TARGET" == "bazel.coverity" ]]; then
  # Coverity Scan version 2017.07 fails to analyze the entirely of the Envoy
  # build when compiled with Clang 5. Revisit when Coverity Scan explicitly
  # supports Clang 5. Until this issue is resolved, run Coverity Scan with
  # the GCC toolchain.
  setup_gcc_toolchain
  echo "bazel Coverity Scan build"
  echo "Building..."
  /build/cov-analysis/bin/cov-build --dir "${ENVOY_BUILD_DIR}"/cov-int bazel build --action_env=LD_PRELOAD "${BAZEL_BUILD_OPTIONS[@]}" \
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
  FUZZ_TEST_TARGETS=("$(bazel query "attr('tags','fuzzer',${TEST_TARGETS[*]})")")
  echo "bazel ASAN libFuzzer build with fuzz tests ${FUZZ_TEST_TARGETS[*]}"
  echo "Building envoy fuzzers and executing 100 fuzz iterations..."
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --config=asan-fuzzer "${FUZZ_TEST_TARGETS[@]}" --test_arg="-runs=10"
  exit 0
elif [[ "$CI_TARGET" == "format_pre" ]]; then
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/ci/format_pre.sh
elif [[ "$CI_TARGET" == "fix_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain

  echo "fix_format..."
  "${ENVOY_SRCDIR}"/tools/code_format/check_format.py fix
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/tools/proto_format/proto_format.sh fix
  exit 0
elif [[ "$CI_TARGET" == "check_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain

  echo "check_format..."
  "${ENVOY_SRCDIR}"/tools/code_format/check_format.py check
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/tools/proto_format/proto_format.sh check
  exit 0
elif [[ "$CI_TARGET" == "docs" ]]; then
  echo "generating docs..."
  # Build docs.
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/docs/build.sh
  exit 0
elif [[ "$CI_TARGET" == "deps" ]]; then

  echo "verifying dependencies..."
  # Validate dependency relationships between core/extensions and external deps.
  "${ENVOY_SRCDIR}"/tools/dependency/validate.py

  # Validate repository metadata.
  echo "check repositories..."
  "${ENVOY_SRCDIR}"/tools/check_repositories.sh
  "${ENVOY_SRCDIR}"/ci/check_repository_locations.sh

  # Run pip requirements tests
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:pip_check "${ENVOY_SRCDIR}"

  exit 0
elif [[ "$CI_TARGET" == "cve_scan" ]]; then
  echo "scanning for CVEs in dependencies..."
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:cve_scan_test
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:cve_scan
  exit 0
elif [[ "$CI_TARGET" == "tooling" ]]; then
  setup_clang_toolchain

  # TODO(phlax): move this to a bazel rule

  echo "Run pytest tooling tests..."
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/testing:all_pytests -- --cov-html /source/generated/tooling "${ENVOY_SRCDIR}"

  echo "Run protoxform test"
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" ./tools/protoxform/protoxform_test.sh

  echo "Run merge active shadow test"
  bazel test "${BAZEL_BUILD_OPTIONS[@]}" //tools/protoxform:merge_active_shadow_test

  echo "check_format_test..."
  "${ENVOY_SRCDIR}"/tools/code_format/check_format_test_helper.sh --log=WARN

  echo "dependency validate_test..."
  "${ENVOY_SRCDIR}"/tools/dependency/validate_test.py

  # Validate the CVE scanner works. We do it here as well as in cve_scan, since this blocks
  # presubmits, but cve_scan only runs async.
  echo "cve_scan_test..."
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:cve_scan_test

  exit 0
elif [[ "$CI_TARGET" == "verify_examples" ]]; then
  run_ci_verify "*" "wasm-cc|win32-front-proxy"
  exit 0
elif [[ "$CI_TARGET" == "verify_build_examples" ]]; then
  run_ci_verify wasm-cc
  exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
