load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "api_type_db_test",
    srcs = ["api_type_db_test.cc"],
    rbe_pool = "2core",
    # MSVC does not allow strings over a certain length, see error C2026
    tags = [
        "no-remote-exec",
        "skip_on_windows",
    ],
    deps = ["//tools/type_whisperer:api_type_db_lib"],
)
