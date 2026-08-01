load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "wuffs_json_cursor_test",
    srcs = ["wuffs_json_cursor_test.cc"],
    deps = [
        "//source/common/json/wuffs_json:wuffs_json_cursor_lib",
        "//test/test_common:status_utility_lib",
    ],
)
