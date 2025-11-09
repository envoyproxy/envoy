#!/usr/bin/env bash

# Run a CI build/test target, e.g. docs, asan.

set -e

# TODO(phlax): Clarify and/or integrate SRCDIR and ENVOY_SRCDIR
export SRCDIR="${SRCDIR:-$PWD}"
export ENVOY_SRCDIR="${ENVOY_SRCDIR:-$PWD}"

CURRENT_SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

# shellcheck source=ci/build_setup.sh
. "${CURRENT_SCRIPT_DIR}"/build_setup.sh

echo "building for ${ENVOY_BUILD_ARCH}"

cd "${SRCDIR}"

if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
  BUILD_ARCH_DIR="/linux/amd64"
elif [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
  BUILD_ARCH_DIR="/linux/arm64"
else
  # Fall back to use the ENVOY_BUILD_ARCH itself.
  BUILD_ARCH_DIR="/linux/${ENVOY_BUILD_ARCH}"
fi

setup_clang_toolchain() {
    if [[ -n "${CLANG_TOOLCHAIN_SETUP}" ]]; then
        return
    fi
    CONFIG_PARTS=()
    if [[ -n "${ENVOY_RBE}" ]]; then
        CONFIG_PARTS+=("remote")
    fi
    if [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
        CONFIG_PARTS+=("arm64")
    fi
    CONFIG_PARTS+=("clang")
    # We only support clang with libc++ now
    CONFIG="$(IFS=- ; echo "${CONFIG_PARTS[*]}")"
    BAZEL_BUILD_OPTIONS+=("--config=${CONFIG}")
    BAZEL_BUILD_OPTION_LIST="${BAZEL_BUILD_OPTIONS[*]}"
    export BAZEL_BUILD_OPTION_LIST
    echo "clang toolchain configured: ${CONFIG}"
}

function collect_build_profile() {
    local output_base
    declare -g build_profile_count=${build_profile_count:-1}
    output_base="$(bazel info "${BAZEL_BUILD_OPTIONS[@]}" output_base)"
    mv -f \
       "${output_base}/command.profile.gz" \
       "${ENVOY_BUILD_PROFILE}/${build_profile_count}-$1.profile.gz" \
        || :
    ((build_profile_count++))
}

function bazel_with_collection() {
  local failed_logs
  declare -r BAZEL_OUTPUT="${ENVOY_SRCDIR}"/bazel.output.txt
  bazel "$@" | tee "${BAZEL_OUTPUT}"
  declare BAZEL_STATUS="${PIPESTATUS[0]}"
  if [ "${BAZEL_STATUS}" != "0" ]
  then
    if [ -d bazel-testlogs ]
    then
        pushd bazel-testlogs
        failed_logs=$(grep "  /build.*test.log" "${BAZEL_OUTPUT}" | sed -e 's/  \/build.*\/testlogs\/\(.*\)/\1/')
        if [[ -n "${failed_logs}" ]]; then
            while read -r f; do
                cp --parents -f "$f" "${ENVOY_FAILED_TEST_LOGS}"
            done <<< "$failed_logs"
            popd
        fi
    fi
    exit "${BAZEL_STATUS}"
  fi
  collect_build_profile "$1"
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
  strip bazel-bin/test/tools/router_check/router_check_tool \
    -o "${BASE_TARGET_DIR}"/"${TARGET_DIR}"/router_check_tool
  strip bazel-bin/test/tools/config_load_check/config_load_check_tool \
    -o "${BASE_TARGET_DIR}"/"${TARGET_DIR}"/config_load_check_tool

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
    CONFIG_ARGS=("--config=sizeopt")
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

  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" "${BUILD_TARGET}" "${CONFIG_ARGS[@]}"
  collect_build_profile "${BINARY_TYPE}"_build

  # Copy the built envoy binary somewhere that we can access outside of the
  # container.
  cp -f bazel-bin/"${ENVOY_BIN}" "${FINAL_DELIVERY_DIR}"/envoy

  if [[ "${COMPILE_TYPE}" == "dbg" || "${COMPILE_TYPE}" == "opt" ]]; then
    # Generate dwp file for debugging since we used split DWARF to reduce binary
    # size
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" "${BUILD_DEBUG_INFORMATION}" "${CONFIG_ARGS[@]}"
    # Copy the debug information
    cp -f bazel-bin/"${ENVOY_BIN}".dwp "${FINAL_DELIVERY_DIR}"/envoy.dwp
  fi

  # Validation tools for the tools image.
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" \
    //test/tools/schema_validator:schema_validator_tool "${CONFIG_ARGS[@]}"
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" \
    //test/tools/router_check:router_check_tool "${CONFIG_ARGS[@]}"
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" \
    //test/tools/config_load_check:config_load_check_tool "${CONFIG_ARGS[@]}"

  # Build su-exec utility
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel -c "${COMPILE_TYPE}" @com_github_ncopa_suexec//:su-exec
  cp_binary_for_image_build "${BINARY_TYPE}" "${COMPILE_TYPE}" "${EXE_NAME}"
}

function bazel_envoy_binary_build() {
  bazel_binary_build "$1" "${ENVOY_BUILD_TARGET}" "${ENVOY_BUILD_DEBUG_INFORMATION}" envoy
}

function bazel_contrib_binary_build() {
  bazel_binary_build "$1" "${ENVOY_CONTRIB_BUILD_TARGET}" "${ENVOY_CONTRIB_BUILD_DEBUG_INFORMATION}" envoy-contrib
}

function bazel_envoy_api_build() {
    setup_clang_toolchain
    export CLANG_TOOLCHAIN_SETUP=1
    export LLVM_CONFIG="${LLVM_ROOT}"/bin/llvm-config
    echo "Run protoxform test"
    bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
        --//tools/api_proto_plugin:default_type_db_target=//tools/testdata/protoxform:fix_protos \
        --//tools/api_proto_plugin:extra_args=api_version:3.7 \
        //tools/protoprint:protoprint_test
    echo "Validating API structure..."
    bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/api:validate_structure "${PWD}/api/envoy"
    echo "Testing API..."
    bazel_with_collection \
        test "${BAZEL_BUILD_OPTIONS[@]}" \
        --remote_download_minimal \
        -c fastbuild \
        @envoy_api//test/... \
        @envoy_api//tools/... \
        @envoy_api//tools:tap2pcap_test
    echo "Building API..."
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
        -c fastbuild @envoy_api//envoy/...
}

function bazel_envoy_api_go_build() {
    setup_clang_toolchain
    GO_IMPORT_BASE="github.com/envoyproxy/go-control-plane"
    GO_TARGETS=(@envoy_api//...)
    read -r -a GO_PROTOS <<< "$(bazel query "${BAZEL_GLOBAL_OPTIONS[@]}" "kind('go_proto_library', ${GO_TARGETS[*]})" | tr '\n' ' ')"
    echo "${GO_PROTOS[@]}" | grep -q envoy_api || echo "No go proto targets found"
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
            --experimental_proto_descriptor_sets_include_source_info \
            --remote_download_outputs=all \
            "${GO_PROTOS[@]}"
    rm -rf build_go
    mkdir -p build_go
    echo "Copying go protos -> build_go"
    BAZEL_BIN="$(bazel info "${BAZEL_BUILD_OPTIONS[@]}" bazel-bin)"
    for GO_PROTO in "${GO_PROTOS[@]}"; do
            # strip @envoy_api//
        RULE_DIR="$(echo "${GO_PROTO:12}" | cut -d: -f1)"
        PROTO="$(echo "${GO_PROTO:12}" | cut -d: -f2)"
        INPUT_DIR="${BAZEL_BIN}/external/envoy_api/${RULE_DIR}/${PROTO}_/${GO_IMPORT_BASE}/${RULE_DIR}"
        OUTPUT_DIR="build_go/${RULE_DIR}"
        mkdir -p "$OUTPUT_DIR"
        if [[ ! -e "$INPUT_DIR" ]]; then
            echo "Unable to find input ${INPUT_DIR}" >&2
            exit 1
        fi
        # echo "Copying go files ${INPUT_DIR} -> ${OUTPUT_DIR}"
        while read -r GO_FILE; do
            cp -a "$GO_FILE" "$OUTPUT_DIR"
            if [[ "$GO_FILE" = *.validate.go ]]; then
                sed -i '1s;^;//go:build !disable_pgv\n;' "$OUTPUT_DIR/$(basename "$GO_FILE")"
            fi
        done <<< "$(find "$INPUT_DIR" -name "*.go")"
    done
}

CI_TARGET=$1
shift

if [[ "$CI_TARGET" =~ bazel.* ]]; then
    ORIG_CI_TARGET="$CI_TARGET"
    CI_TARGET="$(echo "${CI_TARGET}" | cut -d. -f2-)"
    echo "Using \`${ORIG_CI_TARGET}\` is deprecated, please use \`${CI_TARGET}\`"
fi

if [[ $# -ge 1 ]]; then
  COVERAGE_TEST_TARGETS=("$@")
  TEST_TARGETS=("$@")
else
  # Coverage test will add QUICHE tests by itself.
  COVERAGE_TEST_TARGETS=("//test/...")
  if [[ "${CI_TARGET}" == "release" || "${CI_TARGET}" == "release.test_only" ]]; then
    # We test contrib on release only.
    COVERAGE_TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "//contrib/...")
  elif [[ "${CI_TARGET}" == "msan" ]]; then
    COVERAGE_TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "-//test/extensions/...")
  fi
  TEST_TARGETS=("${COVERAGE_TEST_TARGETS[@]}" "@com_github_google_quiche//:ci_tests")
fi

case $CI_TARGET in
    api)
        bazel_envoy_api_build
        if [[ -n "$ENVOY_API_ONLY" ]]; then
            exit 0
        fi
        bazel_envoy_api_go_build
        ;;

    api.go)
        bazel_envoy_api_go_build
        ;;

    api_compat)
        echo "Checking API for breaking changes to protobuf backwards compatibility..."
        BASE_BRANCH_REF=$("${ENVOY_SRCDIR}"/tools/git/last_github_commit.sh)
        COMMIT_TITLE=$(git log -n 1 --pretty='format:%C(auto)%h (%s, %ad)' "${BASE_BRANCH_REF}")
        echo -e "\tUsing base commit ${COMMIT_TITLE}"
        # BAZEL_BUILD_OPTIONS needed for setting the repository_cache param.
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/api_proto_breaking_change_detector:detector_ci \
              "${BASE_BRANCH_REF}"
        ;;

    asan)
        setup_clang_toolchain
        echo "bazel ASAN/UBSAN debug build with tests"
        echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
        bazel_with_collection test \
            --config=asan \
            "${BAZEL_BUILD_OPTIONS[@]}" \
            "${TEST_TARGETS[@]}"
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
        ;;

    cache-create)
        if [[ -z "${ENVOY_CACHE_TARGETS}" ]]; then
            echo "ENVOY_CACHE_TARGETS not set" >&2
            exit 1
        fi
        if [[ -z "${ENVOY_CACHE_ROOT}" ]]; then
            echo "ENVOY_CACHE_ROOT not set" >&2
            exit 1
        fi
        setup_clang_toolchain
        echo "Fetching cache: ${ENVOY_CACHE_TARGETS}"
        bazel --output_user_root="${ENVOY_CACHE_ROOT}" \
              --output_base="${ENVOY_CACHE_ROOT}/base" \
              --nowrite_command_log \
              aquery "deps(${ENVOY_CACHE_TARGETS})" \
              --repository_cache="${ENVOY_REPOSITORY_CACHE}" \
              "${BAZEL_BUILD_EXTRA_OPTIONS[@]}" \
              > /dev/null
          TOTAL_SIZE="$(du -ch "${ENVOY_CACHE_ROOT}" | grep total | tail -n1 | cut -f1)"
          echo "Generated cache: ${TOTAL_SIZE}"
        ;;

    format-api|check_and_fix_proto_format)
        setup_clang_toolchain
        echo "Check and fix proto format ..."
        "${ENVOY_SRCDIR}/ci/check_and_fix_format.sh"
        ;;

    check_proto_format)
        setup_clang_toolchain
        echo "Check proto format ..."
        "${ENVOY_SRCDIR}/tools/proto_format/proto_format.sh" check
        ;;

    clang-tidy)
        setup_clang_toolchain
        export CLANG_TIDY_FIX_DIFF="${ENVOY_TEST_TMPDIR}/lint-fixes/clang-tidy-fixed.diff"
        export FIX_YAML="${ENVOY_TEST_TMPDIR}/lint-fixes/clang-tidy-fixes.yaml"
        export CLANG_TIDY_APPLY_FIXES=1
        mkdir -p "${ENVOY_TEST_TMPDIR}/lint-fixes"
        if [[ -n "$CLANG_TIDY_TARGETS" ]]; then
            read -ra CLANG_TIDY_TARGETS <<< "${CLANG_TIDY_TARGETS}"
        else
            CLANG_TIDY_TARGETS=(
                //contrib/...
                //source/...
                //test/...
                @envoy_api//...)
        fi
        echo "Running clang-tidy on ${CLANG_TIDY_TARGETS[*]}"
        bazel build \
              "${BAZEL_BUILD_OPTIONS[@]}" \
              --config=clang-tidy \
              "${CLANG_TIDY_TARGETS[@]}"
        ;;

    clean|expunge)
        setup_clang_toolchain
        if [[ "$CI_TARGET" == "expunge" ]]; then
            CLEAN_ARGS+=(--expunge)
        fi
        bazel clean "${BAZEL_GLOBAL_OPTIONS[@]}" "${CLEAN_ARGS[@]}"
        ;;

    compile_time_options)
        # See `compile-time-options` in `.bazelrc`
        setup_clang_toolchain
        # This doesn't go into CI but is available for developer convenience.
        echo "bazel with different compiletime options build with tests..."
        TEST_TARGETS=("${TEST_TARGETS[@]/#\/\//@envoy\/\/}")
        echo "Building and testing with wasm=wamr: ${TEST_TARGETS[*]}"
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=compile-time-options \
            --define wasm=wamr \
            -c fastbuild \
            "${TEST_TARGETS[@]}" \
            --test_tag_filters=-nofips \
            --build_tests_only
        echo "Building and testing with wasm=wasmtime: and admin_functionality and admin_html disabled ${TEST_TARGETS[*]}"
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=compile-time-options \
            --define wasm=wasmtime \
            --define admin_functionality=disabled \
            -c fastbuild \
            "${TEST_TARGETS[@]}" \
            --test_tag_filters=-nofips \
            --build_tests_only
        # "--define log_debug_assert_in_release=enabled" must be tested with a release build, so run only
        # these tests under "-c opt" to save time in CI.
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=compile-time-options \
            --define wasm=wasmtime \
            -c opt \
            @envoy//test/common/common:assert_test \
            @envoy//test/server:server_test
        # "--define log_fast_debug_assert_in_release=enabled" must be tested with a release build, so run only these tests under "-c opt" to save time in CI. This option will test only ASSERT()s without SLOW_ASSERT()s, so additionally disable "--define log_debug_assert_in_release" which compiles in both.
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=compile-time-options \
            --define wasm=wasmtime \
            -c opt \
            @envoy//test/common/common:assert_test \
            --define log_fast_debug_assert_in_release=enabled \
            --define log_debug_assert_in_release=disabled
        echo "Building binary with wasm=wasmtime... and logging disabled"
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=compile-time-options \
            --define wasm=wasmtime \
            --define enable_logging=disabled \
            -c fastbuild \
            @envoy//source/exe:envoy-static \
            --build_tag_filters=-nofips
        collect_build_profile build
        ;;

    coverage|fuzz_coverage)
        setup_clang_toolchain
        echo "${CI_TARGET} build with tests ${COVERAGE_TEST_TARGETS[*]}"
        if [[ "$CI_TARGET" == "fuzz_coverage" ]]; then
            export FUZZ_COVERAGE=true
        fi
        export BAZEL_GRPC_LOG="${ENVOY_BUILD_DIR}/grpc.log"
        "${ENVOY_SRCDIR}/test/run_envoy_bazel_coverage.sh" \
            "${COVERAGE_TEST_TARGETS[@]}"
        collect_build_profile coverage
        ;;

    cpu-detection)
        # this can be removed once the expectation is that the integration test
        # is present
        if [[ ! -f "test/server/cgroup_cpu_simple_integration_test.cc" ]]; then
            echo "CPU detection skipped, no integration test available"
            exit 0
        fi
        setup_clang_toolchain
        bazel test \
              "${BAZEL_BUILD_OPTIONS[@]}" \
              //test/server:cgroup_cpu_simple_integration_test
        ;;

    debug)
        setup_clang_toolchain
        echo "Testing ${TEST_TARGETS[*]}"
        # Make sure that there are no regressions to building Envoy with autolink disabled.
        EXTRA_OPTIONS=(
            "--define" "library_autolink=disabled")
        bazel test "${BAZEL_BUILD_OPTIONS[@]}" \
              "${EXTRA_OPTIONS[@]}" \
              -c dbg \
              "${TEST_TARGETS[@]}"
        echo "bazel debug build with tests..."
        bazel_envoy_binary_build debug
        ;;

    debug.server_only)
        setup_clang_toolchain
        echo "bazel debug build..."
        bazel_envoy_binary_build debug
        ;;

    deps)
        setup_clang_toolchain
        echo "dependency validate_test..."
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/dependency:validate_test
        echo "verifying dependencies..."
        # Validate dependency relationships between core/extensions and external deps.
        time bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
             //tools/dependency:validate
        # Validate repository metadata.
        echo "check repositories..."
        "${ENVOY_SRCDIR}/tools/check_repositories.sh"
        echo "check dependencies..."
        # Using todays date as an action_env expires the NIST cache daily, which is the update frequency
        # TODO(phlax): Re-enable cve tests
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:check \
              -- -v warn \
                 -c release_dates releases
        # Run dependabot tests
        echo "Check dependabot ..."
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/dependency:dependatool
        # Disable this pending resolution to https://github.com/envoyproxy/envoy/issues/30286
        # Run pip requirements tests
        # echo "Check pip requirements ..."
        # bazel test "${BAZEL_BUILD_OPTIONS[@]}" \
        #       //tools/base:requirements_test
        ;;

    dev)
        setup_clang_toolchain
        # This doesn't go into CI but is available for developer convenience.
        echo "bazel fastbuild build with tests..."
        echo "Building..."
        bazel_envoy_binary_build fastbuild
        echo "Testing ${TEST_TARGETS[*]}"
        bazel test "${BAZEL_BUILD_OPTIONS[@]}" \
              -c fastbuild "${TEST_TARGETS[@]}"
        ;;

    dev.contrib)
        setup_clang_toolchain
        # This doesn't go into CI but is available for developer convenience.
        echo "bazel fastbuild build with contrib extensions and tests..."
        echo "Building..."
        bazel_contrib_binary_build fastbuild
        echo "Testing ${TEST_TARGETS[*]}"
        bazel test "${BAZEL_BUILD_OPTIONS[@]}" \
              -c fastbuild \
              "${TEST_TARGETS[@]}"
        ;;

    distribution)
        echo "Building distro packages..."
        setup_clang_toolchain
        # Extract the Envoy binary from the tarball
        mkdir -p distribution/custom
        if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
            ENVOY_RELEASE_TARBALL="/build/release/x64/bin/release.tar.zst"
        else
            ENVOY_RELEASE_TARBALL="/build/release/arm64/bin/release.tar.zst"
        fi

        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/zstd \
              -- --stdout \
                 -d "$ENVOY_RELEASE_TARBALL" \
            | tar xfO - envoy > distribution/custom/envoy
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/zstd \
              -- --stdout \
                 -d "$ENVOY_RELEASE_TARBALL" \
            | tar xfO - envoy-contrib > distribution/custom/envoy-contrib
        # Build the packages
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
              --remote_download_toplevel \
              -c opt \
              --//distribution:envoy-binary=//distribution:custom/envoy \
              --//distribution:envoy-contrib-binary=//distribution:custom/envoy-contrib \
              //distribution:packages.tar.gz
        if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
            cp -a bazel-bin/distribution/packages.tar.gz "${ENVOY_BUILD_DIR}/packages.x64.tar.gz"
        else
            cp -a bazel-bin/distribution/packages.tar.gz "${ENVOY_BUILD_DIR}/packages.arm64.tar.gz"
        fi
        ;;

    docker)
        # This is limited to linux x86/arm64 and expects `release` or `release.server_only` to have
        # been run first.
        if ! docker ps &> /dev/null; then
            echo "Unable to build with Docker. If you are running with ci/run_envoy_docker.sh" \
                 "you should set ENVOY_DOCKER_IN_DOCKER=1"
            exit 1
        fi
        if [[ -z "$CI_SHA1" ]]; then
            CI_SHA1="$(git rev-parse HEAD~1)"
            export CI_SHA1
        fi
        ENVOY_ARCH_DIR="$(dirname "${ENVOY_BUILD_DIR}")"
        ENVOY_TARBALL_DIR="${ENVOY_TARBALL_DIR:-${ENVOY_ARCH_DIR}}"
        ENVOY_OCI_DIR="${ENVOY_BUILD_DIR}/${ENVOY_OCI_DIR}"
        export ENVOY_OCI_DIR
        _PLATFORMS=()
        PLATFORM_NAMES=(
            x64:linux/amd64
            arm64:linux/arm64)
        # TODO(phlax): avoid copying bins
        for platform_name in "${PLATFORM_NAMES[@]}"; do
            path="$(echo "${platform_name}" | cut -d: -f1)"
            platform="$(echo "${platform_name}" | cut -d: -f2)"
            bin_folder="${ENVOY_TARBALL_DIR}/${path}/bin"
            if [[ ! -e "${bin_folder}/release.tar.zst" ]]; then
                continue
            fi
            _PLATFORMS+=("$platform")
            if [[ -e "$platform" ]]; then
                rm -rf "$platform"
            fi
            mkdir -p "${platform}"
            cp -a "${bin_folder}"/* "$platform"
        done
        if [[ -z "${_PLATFORMS[*]}" ]]; then
            echo "No tarballs found in ${ENVOY_TARBALL_DIR}, did you run \`release\` first?" >&2
            exit 1
        fi
        PLATFORMS="$(IFS=, ; echo "${_PLATFORMS[*]}")"
        export DOCKER_PLATFORM="$PLATFORMS"
        if [[ -z "$ENVOY_DOCKER_SAVE_IMAGE" ]]; then
            # if you are not saving the images as OCI then load to Docker (ie local build)
            export DOCKER_LOAD_IMAGES=1
        fi
        echo "BUILDING FOR: ${PLATFORMS}"
        "${ENVOY_SRCDIR}/distribution/docker/build.sh"
        ;;

    dockerhub-publish)
        setup_clang_toolchain
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //tools/distribution:update_dockerhub_repository
        ;;

    dockerhub-readme)
        setup_clang_toolchain
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
              --remote_download_toplevel \
              //distribution/dockerhub:readme
        cat bazel-bin/distribution/dockerhub/readme.md
        ;;

    docs)
        setup_clang_toolchain
        echo "generating docs..."
        # Build docs.
        [[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
        rm -rf "${DOCS_OUTPUT_DIR:?}"/*
        mkdir -p "${DOCS_OUTPUT_DIR}"
        if [[ -n "${CI_TARGET_BRANCH}" ]] || [[ -n "${SPHINX_QUIET}" ]]; then
            export SPHINX_RUNNER_ARGS="-v warn"
            BAZEL_BUILD_OPTIONS+=("--action_env=SPHINX_RUNNER_ARGS")
        fi
        if [[ -n "${DOCS_BUILD_RST}" ]]; then
            bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //docs:rst
            cp bazel-bin/docs/rst.tar.gz "$DOCS_OUTPUT_DIR"/envoy-docs-rst.tar.gz
        fi
        DOCS_OUTPUT_DIR="$(realpath "$DOCS_OUTPUT_DIR")"
        bazel "${BAZEL_STARTUP_OPTIONS[@]}" run \
              "${BAZEL_BUILD_OPTIONS[@]}" \
              --//tools/tarball:target=//docs:html \
              //tools/tarball:unpack \
              "$DOCS_OUTPUT_DIR"
        ;;

    fix_proto_format)
        # proto_format.sh needs to build protobuf.
        setup_clang_toolchain
        "${ENVOY_SRCDIR}/tools/proto_format/proto_format.sh" fix
        ;;

    format)
        setup_clang_toolchain
        "${ENVOY_SRCDIR}/ci/format_pre.sh"
        ;;

    fuzz)
        setup_clang_toolchain
        FUZZ_TEST_TARGETS=("$(bazel query "${BAZEL_GLOBAL_OPTIONS[@]}" "attr('tags','fuzzer',${TEST_TARGETS[*]})")")
        echo "bazel ASAN libFuzzer build with fuzz tests ${FUZZ_TEST_TARGETS[*]}"
        echo "Building envoy fuzzers and executing 100 fuzz iterations..."
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=asan-fuzzer \
            "${FUZZ_TEST_TARGETS[@]}" \
            --test_arg="-runs=10"
        ;;

    gcc)
        if [[ -n "${ENVOY_RBE}" ]]; then
            CONFIG_PREFIX="remote-"
        fi
        CONFIG="${CONFIG_PREFIX}gcc"
        BAZEL_BUILD_OPTIONS+=("--config=${CONFIG}")
        echo "gcc toolchain configured: ${CONFIG}"
        echo "bazel fastbuild build with gcc..."
        bazel_envoy_binary_build fastbuild
        echo "Testing ${TEST_TARGETS[*]}"
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            -c fastbuild  \
            --remote_download_minimal \
            -- "${TEST_TARGETS[@]}"
        ;;

    info)
        setup_clang_toolchain
        bazel info "${BAZEL_BUILD_OPTIONS[@]}"
        ;;

    msan)
        setup_clang_toolchain
        echo "bazel MSAN debug build with tests"
        echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
        # msan must comes as first to win library link order.
        bazel_with_collection \
            test --config=msan \
                "${BAZEL_BUILD_OPTIONS[@]}" \
                -- "${TEST_TARGETS[@]}"
        ;;

    publish)
        setup_clang_toolchain
        BUILD_SHA="$(git rev-parse HEAD)"
        ENVOY_COMMIT="${ENVOY_COMMIT:-${BUILD_SHA}}"
        ENVOY_REPO="${ENVOY_REPO:-envoyproxy/envoy}"
        VERSION_DEV="$(cut -d- -f2 < VERSION.txt)"
        PUBLISH_ARGS=(
            --publish-commitish="$ENVOY_COMMIT"
            --publish-commit-message
            --publish-assets=/build/release.signed/release.signed.tar.zst)
        if [[ "$VERSION_DEV" == "dev" ]] || [[ -n "$ENVOY_PUBLISH_DRY_RUN" ]]; then
            PUBLISH_ARGS+=(--dry-run)
        fi
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              @envoy_repo//:publish \
              -- --repo="$ENVOY_REPO" \
                 "${PUBLISH_ARGS[@]}"
        ;;

    release|release.server_only|release.test_only)
        if [[ "$CI_TARGET" == "release" || "$CI_TARGET" == "release.test_only" ]]; then
            # When testing memory consumption, we want to test against exact byte-counts
            # where possible. As these differ between platforms and compile options, we
            # define the 'release' builds as canonical and test them only in CI, so the
            # toolchain is kept consistent. This ifdef is checked in
            # test/common/stats/stat_test_utility.cc when computing
            # Memory::TestUtil::MemoryTest::mode().
            if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
                BAZEL_BUILD_OPTIONS+=("--test_env=ENVOY_MEMORY_TEST_EXACT=true")
            fi
        fi
        setup_clang_toolchain
        # As the binary build package enforces compiler options, adding here to ensure the tests and distribution build
        # reuse settings and any already compiled artefacts, the bundle itself will always be compiled
        # `--stripopt=--strip-all -c opt`
        BAZEL_RELEASE_OPTIONS=(
            --stripopt=--strip-all
            -c opt)
        if [[ "$CI_TARGET" == "release" || "$CI_TARGET" == "release.test_only" ]]; then
            # Run release tests
            echo "Testing with:"
            echo "  targets: ${TEST_TARGETS[*]}"
            echo "  build options: ${BAZEL_BUILD_OPTIONS[*]}"
            echo "  release options:  ${BAZEL_RELEASE_OPTIONS[*]}"
            bazel_with_collection \
                test "${BAZEL_BUILD_OPTIONS[@]}" \
                --remote_download_minimal \
                "${BAZEL_RELEASE_OPTIONS[@]}" \
                "${TEST_TARGETS[@]}"
        fi

        if [[ "$CI_TARGET" == "release.test_only" ]]; then
            exit 0
        fi

        ENVOY_BINARY_DIR="${ENVOY_BUILD_DIR}/bin"
        if [[ -e "${ENVOY_BINARY_DIR}" ]]; then
            echo "Existing output directory found (${ENVOY_BINARY_DIR}), removing ..."
            rm -rf "${ENVOY_BINARY_DIR}"
        fi
        mkdir -p "$ENVOY_BINARY_DIR"

        # Build
        echo "Building with:"
        echo "  build options: ${BAZEL_BUILD_OPTIONS[*]}"
        echo "  release options:  ${BAZEL_RELEASE_OPTIONS[*]}"
        echo "  binary dir:  ${ENVOY_BINARY_DIR}"

        # Build release binaries
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" \
              "${BAZEL_RELEASE_OPTIONS[@]}" \
              --remote_download_outputs=toplevel \
              //distribution/binary:release
        # Copy release binaries to binary export directory
        cp -a \
           "bazel-bin/distribution/binary/release.tar.zst" \
           "${ENVOY_BINARY_DIR}/release.tar.zst"
        # Grab the schema_validator_tool
        # TODO(phlax): bundle this with the release when #26390 is resolved
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" "${BAZEL_RELEASE_OPTIONS[@]}" \
              --remote_download_toplevel \
              //test/tools/schema_validator:schema_validator_tool.stripped
        # Copy schema_validator_tool to binary export directory
        cp -a \
           bazel-bin/test/tools/schema_validator/schema_validator_tool.stripped \
           "${ENVOY_BINARY_DIR}/schema_validator_tool"
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" "${BAZEL_RELEASE_OPTIONS[@]}" \
              --remote_download_toplevel \
              //test/tools/router_check:router_check_tool.stripped
        cp -a \
           bazel-bin/test/tools/router_check/router_check_tool.stripped \
           "${ENVOY_BINARY_DIR}/router_check_tool"
        bazel build "${BAZEL_BUILD_OPTIONS[@]}" "${BAZEL_RELEASE_OPTIONS[@]}" \
              --remote_download_toplevel \
              //test/tools/config_load_check:config_load_check_tool.stripped
        cp -a \
           bazel-bin/test/tools/config_load_check/config_load_check_tool.stripped \
           "${ENVOY_BINARY_DIR}/config_load_check_tool"
        echo "Release files created in ${ENVOY_BINARY_DIR}"
        ;;

    release.server_only.binary)
        setup_clang_toolchain
        echo "bazel release build..."
        bazel_envoy_binary_build release
        ;;

    release.signed)
        echo "Signing binary packages..."
        setup_clang_toolchain
        bazel build \
              "${BAZEL_BUILD_OPTIONS[@]}" \
              //distribution:signed
        cp -a bazel-bin/distribution/release.signed.tar.zst "${BUILD_DIR}/envoy/"
        ;;

    sizeopt)
        setup_clang_toolchain
        echo "Testing ${TEST_TARGETS[*]}"
        bazel_with_collection \
            test "${BAZEL_BUILD_OPTIONS[@]}" \
            --config=sizeopt \
            "${TEST_TARGETS[@]}"
        echo "bazel size optimized build with tests..."
        bazel_envoy_binary_build sizeopt
        ;;

    sizeopt.server_only)
        setup_clang_toolchain
        echo "bazel size optimized build..."
        bazel_envoy_binary_build sizeopt
        ;;

    tsan)
        setup_clang_toolchain
        echo "bazel TSAN debug build with tests"
        echo "Building and testing envoy tests ${TEST_TARGETS[*]}"
        bazel_with_collection \
            test \
             --config=tsan \
            "${BAZEL_BUILD_OPTIONS[@]}" \
             "${TEST_TARGETS[@]}"
        ;;

    verify_distro)
        # this can be required if any python deps require compilation
        setup_clang_toolchain
        if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
            PACKAGE_BUILD=/build/distribution/x64/packages.x64.tar.gz
        else
            PACKAGE_BUILD=/build/distribution/arm64/packages.arm64.tar.gz
        fi
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              //distribution:verify_packages \
              "$PACKAGE_BUILD"
        ;;

    verify-distroless)
        docker build -f ci/Dockerfile-distroless-testing -t distroless-testing .
        docker run --rm distroless-testing
        ;;

    verify_examples)
        DEV_CONTAINER_ID=$(docker inspect --format='{{.Id}}' envoyproxy/envoy:dev)
        bazel run --config=ci \
                  --action_env="DEV_CONTAINER_ID=${DEV_CONTAINER_ID}" \
                  --host_action_env="DEV_CONTAINER_ID=${DEV_CONTAINER_ID}" \
                  --sandbox_writable_path="${HOME}/.docker/" \
                  --sandbox_writable_path="$HOME" \
                  @envoy_examples//:verify_examples
        ;;

    verify.trigger)
        setup_clang_toolchain
        WORKFLOW="envoy-publish.yml"
        # * Note on vars *
        # `ENVOY_REPO`: Should always be envoyproxy/envoy unless testing
        # `ENVOY_BRANCH`: Target branch for PRs, source branch for others
        # `COMMIT`: This may be a merge commit in a PR
        # `ENVOY_COMMIT`: The actual last commit of branch/PR
        # `ENVOY_HEAD_REF`: must also be set in PRs to provide a unique key for job grouping,
        #   cancellation, and to discriminate from other branch CI
        COMMIT="$(git rev-parse HEAD)"
        ENVOY_COMMIT="${ENVOY_COMMIT:-${COMMIT}}"
        ENVOY_REPO="${ENVOY_REPO:-envoyproxy/envoy}"
        echo "Trigger workflow (${WORKFLOW})"
        echo "  Repo: ${ENVOY_REPO}"
        echo "  Branch: ${ENVOY_BRANCH}"
        echo "  Ref: ${COMMIT}"
        echo "  Inputs:"
        echo "    sha: ${ENVOY_COMMIT}"
        echo "    head_ref: ${ENVOY_HEAD_REF}"
        GITHUB_APP_KEY="$(echo "$GITHUB_TOKEN" | base64 -d -w0)"
        export GITHUB_APP_KEY
        INPUTS="{\"ref\":\"$COMMIT\",\"sha\":\"$ENVOY_COMMIT\",\"head_ref\":\"$ENVOY_HEAD_REF\"}"
        bazel run "${BAZEL_BUILD_OPTIONS[@]}" \
              @envoy_repo//:trigger \
              -- --repo="$ENVOY_REPO" \
                 --trigger-app-id="$GITHUB_APP_ID" \
                 --trigger-installation-id="$GITHUB_INSTALL_ID" \
                 --trigger-ref="$ENVOY_BRANCH" \
                 --trigger-workflow="$WORKFLOW" \
                 --trigger-inputs="$INPUTS"
        ;;

    refresh_compdb)
        setup_clang_toolchain
        # Override the BAZEL_STARTUP_OPTIONS to setting different output directory.
        # So the compdb headers won't be overwritten by another bazel run.
        for i in "${!BAZEL_STARTUP_OPTIONS[@]}"; do
            if [[ ${BAZEL_STARTUP_OPTIONS[i]} == "--output_base"* ]]; then
                COMPDB_OUTPUT_BASE="${BAZEL_STARTUP_OPTIONS[i]}"-envoy-compdb
                BAZEL_STARTUP_OPTIONS[i]="${COMPDB_OUTPUT_BASE}"
                BAZEL_STARTUP_OPTION_LIST="${BAZEL_STARTUP_OPTIONS[*]}"
                export BAZEL_STARTUP_OPTION_LIST
            fi
        done

        if [[ -z "${SKIP_PROTO_FORMAT}" ]]; then
            "${CURRENT_SCRIPT_DIR}/../tools/proto_format/proto_format.sh" fix
        fi

        read -ra ENVOY_GEN_COMPDB_OPTIONS <<< "${ENVOY_GEN_COMPDB_OPTIONS:-}"

        "${CURRENT_SCRIPT_DIR}/../tools/gen_compilation_database.py" \
            "${ENVOY_GEN_COMPDB_OPTIONS[@]}"
        # Kill clangd to reload the compilation database
        pkill clangd || :
        ;;

    *)
        echo "Invalid do_ci.sh target (${CI_TARGET}), see ci/README.md for valid targets."
        exit 1
        ;;
esac
