#!/usr/bin/bash.exe

set -ex

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

/c/windows/system32/cmd.exe /c "rmdir /s /q C:\Program Files\Git\tmp && mkdir C:\tmp && mklink /d C:\Program Files\Git\tmp C:\tmp && mklink /d D:\tmp C:\tmp"

echo "check on the directory setup"
ls -lah / && ls -lah /tmp
ls -lah /c && ls -lah /c/tmp
ls -lah /d && ls -lah /d/tmp

. "$(dirname "$0")"/setup_cache.sh

# TODO(dio): Put in windows/.bazelrc.
export PATH="/c/Program Files (x86)/Windows Kits/10/bin/10.0.17763.0/x64":$PATH

BAZEL_STARTUP_OPTIONS="--noworkspace_rc --bazelrc=windows/.bazelrc --output_base=c:/_eb"
BAZEL_BUILD_OPTIONS="-c fastbuild --config=msvc-cl --show_task_finish --verbose_failures \
  --test_output=all ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //bazel/... --build_tag_filters=-skip_on_windows

bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //source/exe:envoy-static

# bazel ${BAZEL_STARTUP_OPTIONS} test ${BAZEL_BUILD_OPTIONS} //test/... --test_tag_filters=-skip_on_windows --build_tests_only --test_summary=terse --test_output=errors
