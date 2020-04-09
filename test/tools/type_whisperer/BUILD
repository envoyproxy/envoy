licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "api_type_db_test",
    srcs = ["api_type_db_test.cc"],
    deps = ["//tools/type_whisperer:api_type_db_lib"],
)
