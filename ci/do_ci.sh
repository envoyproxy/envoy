#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e


build_setup_args=""
if [[ "$1" == "format" || "$1" == "fix_proto_format" || "$1" == "check_proto_format" || "$1" == "docs" ||  \
          "$1" == "bazel.clang_tidy" || "$1" == "bazel.distribution" \
          || "$1" == "deps" || "$1" == "verify_examples" || "$1" == "publish" \
          || "$1" == "verify_distro" ]]; then
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

function cp_binary_for_image_build() {
  local BINARY_TYPE="$1"
  local COMPILE_TYPE="$2"
  local EXE_NAME="$3"

  # TODO(mattklein123): Replace this with caching and a different job which creates images.
  local BASE_TARGET_DIR="${ENVOY_SRCDIR}${BUILD_ARCH_DIR}"
  local TARGET_DIR=build_"${EXE_NAME}"_"${BINARY_TYPE}"
  local FINAL_DELIVERY_DIR="${ENVOY_DELIVERY_DIR}"/"${EXE_NAME}"

  echo "Copying binary for image build..."
  mkdir -p "${BASE_TARGET_DIR}"/"${TARGET_DIR}"
  cp -f "${FINAL_DELIVERY_DIR}"/envoy "${BASE_TARGET_DIR}"/"${TARGET_DIR}"
  if [[ "${COMPILE_TYPE}" == "dbg" || "${COMPILE_TYPE}" == "opt" ]]; then
    cp -f "${FINAL_DELIVERY_DIR}"/envoy.dwp "${BASE_TARGET_DIR}"/"${TARGET_DIR}"
  fi

  # Tools for the tools image. Strip to save size.
  strip bazel-bin/test/tools/schema_validator/schema_validator_tool \
    -o "${BASE_TARGET_DIR}"/"${TARGET_DIR}"/schema_validator_tool

  # Copy the su-exec utility binary into the image
  cp -f bazel-bin/external/com_github_ncopa_suexec/su-exec "${BASE_TARGET_DIR}"/"${TARGET_DIR}"

  # Stripped binaries for the debug image.
  mkdir -p "${BASE_TARGET_DIR}"/"${TARGET_DIR}"_stripped
  strip "${FINAL_DELIVERY_DIR}"/envoy -o "${BASE_TARGET_DIR}"/"${TARGET_DIR}"_stripped/envoy

  # only if BUILD_REASON exists (running in AZP)
  if [[ "${BUILD_REASON}" ]]; then
    # Copy for azp which doesn't preserve permissions
    tar czf "${ENVOY_BUILD_DIR}"/"${EXE_NAME}"_binary.tar.gz -C "${BASE_TARGET_DIR}" "${TARGET_DIR}" "${TARGET_DIR}"_stripped

    # Remove binaries to save space
    rm -rf "${BASE_TARGET_DIR:?}"/"${TARGET_DIR}" "${BASE_TARGET_DIR:?}"/"${TARGET_DIR}"_stripped "${FINAL_DELIVERY_DIR:?}"/envoy{,.dwp} \
      bazel-bin/"${ENVOY_BIN}"{,.dwp}
  fi
}

function bazel_binary_build() {
  local BINARY_TYPE="$1"
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

  local BUILD_TARGET="$2"
  local BUILD_DEBUG_INFORMATION="$3"
  local EXE_NAME="$4"
  local FINAL_DELIVERY_DIR="${ENVOY_DELIVERY_DIR}"/"${EXE_NAME}"
  mkdir -p "${FINAL_DELIVERY_DIR}"

  echo "Building (type=${BINARY_TYPE} target=${BUILD_TARGET} debug=${BUILD_DEBUG_INFORMATION} name=${EXE_NAME})..."
  ENVOY_BIN=$(echo "${BUILD_TARGET}" | sed -e 's#^@\([^/]*\)/#external/\1#;s#^//##;s#:#/#')
  echo "ENVOY_BIN=${ENVOY_BIN}"

  # This is a workaround for https://github.com/bazelbuild/bazel/issues/11834
  [[ -n "${ENVOY_RBE}" ]] && rm -rf bazel-bin/"${ENVOY_BIN}"*

  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" "${BUILD_TARGET}" ${CONFIG_ARGS}
  collect_build_profile "${BINARY_TYPE}"_build

  # Copy the built envoy binary somewhere that we can access outside of the
  # container.
  cp -f bazel-bin/"${ENVOY_BIN}" "${FINAL_DELIVERY_DIR}"/envoy

  if [[ "${COMPILE_TYPE}" == "dbg" || "${COMPILE_TYPE}" == "opt" ]]; then
    # Generate dwp file for debugging since we used split DWARF to reduce binary
    # size
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" "${BUILD_DEBUG_INFORMATION}" ${CONFIG_ARGS}
    # Copy the debug information
    cp -f bazel-bin/"${ENVOY_BIN}".dwp "${FINAL_DELIVERY_DIR}"/envoy.dwp
  fi

  # Validation tools for the tools image.
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" \
    //test/tools/schema_validator:schema_validator_tool ${CONFIG_ARGS}

  # Build su-exec utility
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c "${COMPILE_TYPE}" external:su-exec
  cp_binary_for_image_build "${BINARY_TYPE}" "${COMPILE_TYPE}" "${EXE_NAME}"
}

function bazel_envoy_binary_build() {
  bazel_binary_build "$1" "${ENVOY_BUILD_TARGET}" "${ENVOY_BUILD_DEBUG_INFORMATION}" envoy
}

function bazel_contrib_binary_build() {
  bazel_binary_build "$1" "${ENVOY_CONTRIB_BUILD_TARGET}" "${ENVOY_CONTRIB_BUILD_DEBUG_INFORMATION}" envoy-contrib
}

function run_process_test_result() {
  if [[ -z "$CI_SKIP_PROCESS_TEST_RESULTS" ]] && [[ $(find "$TEST_TMPDIR" -name "*_attempt.xml" 2> /dev/null) ]]; then
      echo "running flaky test reporting script"
      bazel run "${BAZEL_BUILD_OPTIONS[@]}" //ci/flaky_test:process_xml "$CI_TARGET"
  else
      echo "no flaky test results found"
  fi
}

function run_ci_verify () {
  echo "verify examples..."
  OCI_TEMP_DIR="${ENVOY_DOCKER_BUILD_DIR}/image"
  mkdir -p "${OCI_TEMP_DIR}"

  IMAGES=("envoy" "envoy-contrib" "envoy-google-vrp")

  for IMAGE in "${IMAGES[@]}"; do
    tar xvf "${ENVOY_DOCKER_BUILD_DIR}/docker/${IMAGE}.tar" -C "${OCI_TEMP_DIR}"
    skopeo copy "oci:${OCI_TEMP_DIR}" "docker-daemon:envoyproxy/${IMAGE}-dev:latest"
    rm -rf "${OCI_TEMP_DIR:?}/*"
  done

  rm -rf "${OCI_TEMP_DIR:?}"

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
  if [[ "$CI_TARGET" == "bazel.release" ]]; then
    # We test contrib on release only.
    COVERAGE_TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "//contrib/...")
  elif [[ "${CI_TARGET}" == "bazel.msan" ]]; then
    COVERAGE_TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "-//test/extensions/...")
  fi
  TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "@com_github_google_quiche//:ci_tests" "//mobile/test/...")
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

  echo "bazel release build..."
  bazel_envoy_binary_build release

  echo "bazel contrib release build..."
  bazel_contrib_binary_build release

  exit 0
elif [[ "$CI_TARGET" == "bazel.distribution" ]]; then
  echo "Building distro packages..."

  setup_clang_toolchain

  # By default the packages will be signed by the first available key.
  # If there is no key available, a throwaway key is created
  # and the packages signed with it, for the purpose of testing only.
  if ! gpg --list-secret-keys "*"; then
      export PACKAGES_MAINTAINER_NAME="Envoy CI"
      export PACKAGES_MAINTAINER_EMAIL="envoy-ci@for.testing.only"
      BAZEL_BUILD_OPTIONS+=(
          "--action_env=PACKAGES_GEN_KEY=1"
          "--action_env=PACKAGES_MAINTAINER_NAME"
          "--action_env=PACKAGES_MAINTAINER_EMAIL")
  fi

  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c opt //distribution:packages.tar.gz
  if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
      cp -a bazel-bin/distribution/packages.tar.gz "${ENVOY_BUILD_DIR}/packages.x64.tar.gz"
  else
      cp -a bazel-bin/distribution/packages.tar.gz "${ENVOY_BUILD_DIR}/packages.arm64.tar.gz"
  fi
  exit 0
elif [[ "$CI_TARGET" == "bazel.release.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel release build..."
  bazel_envoy_binary_build release
  exit 0
elif [[ "$CI_TARGET" == "bazel.sizeopt.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel size optimized build..."
  bazel_envoy_binary_build sizeopt
  exit 0
elif [[ "$CI_TARGET" == "bazel.sizeopt" ]]; then
  setup_clang_toolchain
  echo "Testing ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --config=sizeopt "${TEST_TARGETS[@]}"

  echo "bazel size optimized build with tests..."
  bazel_envoy_binary_build sizeopt
  exit 0
elif [[ "$CI_TARGET" == "bazel.gcc" ]]; then
  BAZEL_BUILD_OPTIONS+=("--test_env=HEAPCHECK=")
  setup_gcc_toolchain

  echo "Testing ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild -- "${TEST_TARGETS[@]}"

  echo "bazel release build with gcc..."
  bazel_envoy_binary_build fastbuild
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug" ]]; then
  setup_clang_toolchain
  echo "Testing ${TEST_TARGETS[*]}"
  # Make sure that there are no regressions to building Envoy with autolink disabled.
  EXTRA_OPTIONS=(
    "--define" "library_autolink=disabled")
  bazel test "${BAZEL_BUILD_OPTIONS[@]}"  "${EXTRA_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}"

  echo "bazel debug build with tests..."
  bazel_envoy_binary_build debug
  exit 0
elif [[ "$CI_TARGET" == "bazel.debug.server_only" ]]; then
  setup_clang_toolchain
  echo "bazel debug build..."
  bazel_envoy_binary_build debug
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
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -- "${TEST_TARGETS[@]}"
  exit 0
elif [[ "$CI_TARGET" == "bazel.dev" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with tests..."
  echo "Building..."
  bazel_envoy_binary_build fastbuild

  echo "Testing ${TEST_TARGETS[*]}"
  bazel test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild "${TEST_TARGETS[@]}"
  exit 0
elif [[ "$CI_TARGET" == "bazel.dev.contrib" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with contrib extensions and tests..."
  echo "Building..."
  bazel_contrib_binary_build fastbuild

  echo "Testing ${TEST_TARGETS[*]}"
  bazel test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild "${TEST_TARGETS[@]}"
  exit 0
elif [[ "$CI_TARGET" == "bazel.compile_time_options" ]]; then
  # Right now, none of the available compile-time options conflict with each other. If this
  # changes, this build type may need to be broken up.
  COMPILE_TIME_OPTIONS=(
    "--define" "admin_html=disabled"
    "--define" "signal_trace=disabled"
    "--define" "hot_restart=disabled"
    "--define" "google_grpc=disabled"
    "--define" "boringssl=fips"
    "--define" "log_debug_assert_in_release=enabled"
    "--define" "path_normalization_by_default=true"
    "--define" "deprecated_features=disabled"
    "--define" "tcmalloc=gperftools"
    "--define" "zlib=ng"
    "--define" "uhv=enabled"
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

  echo "Building and testing with wasm=wasmtime: and admin_functionality and admin_html disabled ${TEST_TARGETS[*]}"
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --define wasm=wasmtime --define admin_html=disabled --define admin_functionality=disabled "${COMPILE_TIME_OPTIONS[@]}" -c dbg "${TEST_TARGETS[@]}" --test_tag_filters=-nofips --build_tests_only

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
  echo "Validate Golang protobuf generation..."
  "${ENVOY_SRCDIR}"/tools/api/generate_go_protobuf.py
  echo "Testing API..."
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild @envoy_api//test/... @envoy_api//tools/... \
    @envoy_api//tools:tap2pcap_test
  echo "Building API..."
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" -c fastbuild @envoy_api//envoy/...
  exit 0
elif [[ "$CI_TARGET" == "bazel.api_compat" ]]; then
  echo "Checking API for breaking changes to protobuf backwards compatibility..."
  BASE_BRANCH_REF=$("${ENVOY_SRCDIR}"/tools/git/last_github_commit.sh)
  COMMIT_TITLE=$(git log -n 1 --pretty='format:%C(auto)%h (%s, %ad)' "${BASE_BRANCH_REF}")
  echo -e "\tUsing base commit ${COMMIT_TITLE}"
  # BAZEL_BUILD_OPTIONS needed for setting the repository_cache param.
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/api_proto_breaking_change_detector:detector_ci "${BASE_BRANCH_REF}"
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
elif [[ "$CI_TARGET" == "bazel.fuzz" ]]; then
  setup_clang_toolchain
  FUZZ_TEST_TARGETS=("$(bazel query "attr('tags','fuzzer',${TEST_TARGETS[*]})")")
  echo "bazel ASAN libFuzzer build with fuzz tests ${FUZZ_TEST_TARGETS[*]}"
  echo "Building envoy fuzzers and executing 100 fuzz iterations..."
  bazel_with_collection test "${BAZEL_BUILD_OPTIONS[@]}" --config=asan-fuzzer "${FUZZ_TEST_TARGETS[@]}" --test_arg="-runs=10"
  exit 0
elif [[ "$CI_TARGET" == "format" ]]; then
  setup_clang_toolchain
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/ci/format_pre.sh
elif [[ "$CI_TARGET" == "fix_proto_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/tools/proto_format/proto_format.sh fix
  exit 0
elif [[ "$CI_TARGET" == "check_proto_format" ]]; then
  # proto_format.sh needs to build protobuf.
  setup_clang_toolchain
  echo "Run protoxform test"
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
        --//tools/api_proto_plugin:default_type_db_target=//tools/testdata/protoxform:fix_protos \
        --//tools/api_proto_plugin:extra_args=api_version:3.7 \
        //tools/protoprint:protoprint_test
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/tools/proto_format/proto_format.sh check
  exit 0
elif [[ "$CI_TARGET" == "docs" ]]; then
  setup_clang_toolchain

  echo "generating docs..."
  # Build docs.
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS[*]}" "${ENVOY_SRCDIR}"/docs/build.sh
  exit 0
elif [[ "$CI_TARGET" == "deps" ]]; then
  setup_clang_toolchain

  echo "dependency validate_test..."
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:validate_test

  echo "verifying dependencies..."
  # Validate dependency relationships between core/extensions and external deps.
  time bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:validate

  # Validate repository metadata.
  echo "check repositories..."
  "${ENVOY_SRCDIR}"/tools/check_repositories.sh

  echo "check dependencies..."
  # Using todays date as an action_env expires the NIST cache daily, which is the update frequency
  TODAY_DATE=$(date -u -I"date")
  export TODAY_DATE
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:check \
        --action_env=TODAY_DATE \
        -- -v warn \
           -c cves release_dates releases

  # Run pip requirements tests
  echo "check pip..."
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:pip_check

  exit 0
elif [[ "$CI_TARGET" == "verify_examples" ]]; then
  run_ci_verify "*" "win32-front-proxy|shared"
  exit 0
elif [[ "$CI_TARGET" == "verify_distro" ]]; then
    if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
        PACKAGE_BUILD=/build/bazel.distribution/packages.x64.tar.gz
    else
        PACKAGE_BUILD=/build/bazel.distribution.arm64/packages.arm64.tar.gz
    fi
    bazel run "${BAZEL_BUILD_OPTIONS[@]}" //distribution:verify_packages "$PACKAGE_BUILD"
    exit 0
elif [[ "$CI_TARGET" == "publish" ]]; then
    bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/project:publish
    exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
