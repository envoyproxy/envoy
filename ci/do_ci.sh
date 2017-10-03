#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

. "$(dirname "$0")"/build_setup.sh
echo "building using ${NUM_CPUS} CPUs"

function bazel_release_binary_build() {
  echo "Building..."
  cd "${ENVOY_CI_DIR}"
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c opt //source/exe:envoy-static.stamped
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-genfiles/source/exe/envoy-static.stamped \
    "${ENVOY_DELIVERY_DIR}"/envoy
}

function bazel_debug_binary_build() {
  echo "Building..."
  cd "${ENVOY_CI_DIR}"
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c dbg //source/exe:envoy-static.stamped
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-genfiles/source/exe/envoy-static.stamped \
    "${ENVOY_DELIVERY_DIR}"/envoy-debug
}

if [[ "$1" == "bazel.release" ]]; then
  setup_gcc_toolchain
  echo "bazel release build with tests..."
  bazel_release_binary_build
  echo "Testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c opt //test/...
  # TODO(mattklein123): Replace this with caching and a different job which creates images.
  echo "Copying for image build..."
  mkdir -p build_release
  cp -f "$ENVOY_BUILD_DIR"/envoy/source/exe/envoy ./build_release
  exit 0
elif [[ "$1" == "bazel.release.server_only" ]]; then
  setup_gcc_toolchain
  echo "bazel release build..."
  bazel_release_binary_build
  exit 0
elif [[ "$1" == "bazel.debug" ]]; then
  setup_gcc_toolchain
  echo "bazel debug build with tests..."
  bazel_debug_binary_build
  echo "Testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg //test/...
  exit 0
elif [[ "$1" == "bazel.debug.server_only" ]]; then
  setup_gcc_toolchain
  echo "bazel debug build..."
  bazel_debug_binary_build
  exit 0
elif [[ "$1" == "bazel.asan" ]]; then
  setup_clang_toolchain
  echo "bazel ASAN/UBSAN debug build with tests..."
  # Due to Travis CI limits, we build and run the single fat coverage test binary rather than
  # build O(100) * O(200MB) static test binaries. This saves 20GB of disk space, see #1400.
  cd "${ENVOY_BUILD_DIR}"
  NO_GCOV=1 "${ENVOY_SRCDIR}"/test/coverage/gen_build.sh
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg --config=clang-asan @envoy//test/coverage:coverage_tests \
    //:echo2_integration_test //:envoy_binary_test
  exit 0

  # remove the output directory and do coverage build
elif [[ "$1" == "bazel.covbuild" ]]; then
  setup_clang_toolchain
  which lcov  # verify you're in the right docker image.
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  echo "Building and testing..."
  # you will regularly have problems with stale files if you do not do this.
  bazel clean --expunge  
  # This commented out but was useful for rapid iterating on build options since it builds and runs
  # far faster than the full coverage test.
  # bazel test -c dbg ${BAZEL_TEST_OPTIONS}  @envoy//test/common/http:utility_test
  bazel test  -c dbg ${BAZEL_TEST_OPTIONS}  @envoy//test/coverage:coverage_tests

# generate coverage reports
elif [[ "$1" == "bazel.cov" ]]; then
  setup_clang_toolchain
  # Yes, this is my custom bazel directory. I did claim these were horrible hacks, no?
  cd /build/tmp/_bazel_bazel/400406edc57d332f0b9b805d2b8e33a1/
  # code in foo.sh was easy to mess with in and outside of docker.  Pasted below.
  sh ../foo.sh
  echo "collecting"
  # otherwise you get way more complaints of "can not find this file" when generating reports.
  ln -s execroot/envoy/bazel-out/ bazel-out
  echo "creating sh"
  # the internet told me this was useful for running llvmcov
  echo '#!/bin/bash' > llvm-gcov.sh; echo 'exec /usr/lib/llvm-5.0/bin/llvm-cov gcov "$@"' >> llvm-gcov.sh
  chmod a+x llvm-gcov.sh;
  echo " running lcov"
  # Combine all the gcna/gcno data into the single cov.info
  lcov --directory . --base-directory .   --gcov-tool ./llvm-gcov.sh  --capture -o cov.info
  # strip a bunch of directories we don't care about from the cov.info
  # the path simplification didn't seem to work so could probably be cut.
  lcov --remove cov.info '/usr/include/*' 'bazel-out/local-dbg/genfiles/*'\
     'external/envoy/ci/prebuilt/thirdparty/*' 'external/envoy/test/*' \
     -p 'bazel-out' -o cov2.info
  echo " running genhtml\n\n"
  # generates all the HTML files from the stripped cov2.info
  genhtml cov2.info -o output
  # makes the human-readable links less onerous since lcov -p didn't do it for me.
  sed -i 's/>bazel-out.local-dbg.bin.external.envoy./>/' output/index.html
  echo "done"
  exit 0


elif [[ "$1" == "bazel.tsan" ]]; then
  setup_clang_toolchain
  echo "bazel TSAN debug build with tests..."
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg --config=clang-tsan @envoy//test/... \
    //:echo2_integration_test //:envoy_binary_test
  exit 0
elif [[ "$1" == "bazel.dev" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with tests..."
  cd "${ENVOY_CI_DIR}"
  echo "Building..."
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c fastbuild //source/exe:envoy-static
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container for developers.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-bin/source/exe/envoy-static \
    "${ENVOY_DELIVERY_DIR}"/envoy-fastbuild
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c fastbuild //test/...
  exit 0
elif [[ "$1" == "bazel.ipv6_tests" ]]; then
  # This is around until Circle supports IPv6. We try to run a limited set of IPv6 tests as fast
  # as possible for basic sanity testing.
  setup_clang_toolchain
  echo "Testing..."
  cd "${ENVOY_CI_DIR}"
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c fastbuild //test/integration/... //test/common/network/...
  exit 0
elif [[ "$1" == "bazel.coverage" ]]; then
  setup_gcc_toolchain
  echo "bazel coverage build with tests..."
  export GCOVR="/thirdparty/gcovr/scripts/gcovr"
  export GCOVR_DIR="${ENVOY_BUILD_DIR}/bazel-envoy"
  export TESTLOGS_DIR="${ENVOY_BUILD_DIR}/bazel-testlogs"
  export WORKSPACE=ci
  # There is a bug in gcovr 3.3, where it takes the -r path,
  # in our case /source, and does a regex replacement of various
  # source file paths during HTML generation. It attempts to strip
  # out the prefix (e.g. /source), but because it doesn't do a match
  # and only strip at the start of the string, it removes /source from
  # the middle of the string, corrupting the path. The workaround is
  # to point -r in the gcovr invocation in run_envoy_bazel_coverage.sh at
  # some Bazel created symlinks to the source directory in its output
  # directory. Wow.
  cd "${ENVOY_BUILD_DIR}"
  SRCDIR="${GCOVR_DIR}" "${ENVOY_SRCDIR}"/test/run_envoy_bazel_coverage.sh
  rsync -av "${ENVOY_BUILD_DIR}"/bazel-envoy/generated/coverage/ "${ENVOY_COVERAGE_DIR}"
  exit 0
elif [[ "$1" == "fix_format" ]]; then
  echo "fix_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py fix
  exit 0
elif [[ "$1" == "check_format" ]]; then
  echo "check_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py check
  exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
