load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_sh_test(
    name = "router_tool_test",
    size = "large",
    srcs = ["route_tests.sh"],
    cc_binary = ["//test/tools/router_check:router_check_tool"],
    data = [
        ":configs",
    ],
    rbe_pool = "2core",
    tags = ["nocoverage"],
)

envoy_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    data = [
        ":configs",
    ],
    rbe_pool = "2core",
    deps = [
        "//test/tools/router_check:router_check_main_lib",
    ],
)

filegroup(
    name = "configs",
    srcs = glob([
        "config/*.yaml",
        "config/*.json",
        "config/*.pb_text",
    ]),
)
