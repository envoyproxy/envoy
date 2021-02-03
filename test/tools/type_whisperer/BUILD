load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "api_type_db_test",
    srcs = ["api_type_db_test.cc"],
    # MSVC does not allow strings over a certain length, see error C2026
    tags = ["skip_on_windows"],
    deps = ["//tools/type_whisperer:api_type_db_lib"],
)
