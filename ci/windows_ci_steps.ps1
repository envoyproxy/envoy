#powershell.exe -Command

$BAZEL_STARTUP_OPTIONS = "--bazelrc=windows/.bazelrc --output_base=c:/_eb"
$BAZEL_BUILD_OPTIONS = "--config=msvc-cl --show_task_finish --verbose_failures --test_output=all $BAZEL_BUILD_EXTRA_OPTIONS $BAZEL_EXTRA_TEST_OPTIONS"

powershell -C bazel.exe $BAZEL_STARTUP_OPTIONS build $BAZEL_BUILD_OPTIONS //bazel/... --build_tag_filters=-skip_on_windows
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

powershell -C bazel.exe $BAZEL_STARTUP_OPTIONS build $BAZEL_BUILD_OPTIONS //source/exe:envoy-static
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

powershell -C bazel.exe $BAZEL_STARTUP_OPTIONS test $BAZEL_BUILD_OPTIONS //test/... --test_tag_filters=-manual --test_tag_filters=-skip_on_windows --build_tests_only --test_summary=terse --test_output=errors
