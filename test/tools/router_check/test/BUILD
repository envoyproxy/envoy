package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_sh_test")

envoy_sh_test(
    name = "router_tool_test",
    srcs = ["route_tests.sh"],
    data = [
        ":configs",
        "//test/tools/router_check:router_check_tool",
    ],
)

filegroup(
    name = "configs",
    srcs = glob(["config/*.json"]),
)
