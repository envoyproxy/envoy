load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_sh_test(
    name = "router_tool_test",
    srcs = ["route_tests.sh"],
    cc_binary = ["//test/tools/router_check:router_check_tool"],
    data = [
        ":configs",
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
