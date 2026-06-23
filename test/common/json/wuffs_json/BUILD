load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
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
    ],
)

envoy_cc_benchmark_binary(
    name = "wuffs_json_perf_test",
    srcs = ["wuffs_json_perf_test.cc"],
    deps = [
        "//source/common/json/wuffs_json:wuffs_json_cursor_lib",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_benchmark_binary(
    name = "wuffs_vs_mcp_perf_test",
    srcs = ["wuffs_vs_mcp_perf_test.cc"],
    deps = [
        "//source/common/json/wuffs_json:wuffs_json_cursor_lib",
        "//source/extensions/filters/http/mcp:mcp_json_parser_lib",
        "@nlohmann_json//:json",
    ],
)
