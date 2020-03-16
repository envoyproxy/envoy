licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_sh_test",
)

envoy_package()

envoy_sh_test(
    name = "router_tool_test",
    srcs = ["route_tests.sh"],
    data = [
        ":configs",
        "//test/tools/router_check:router_check_tool",
    ],
    # TODO: This script invocation does not work on Windows, see: https://github.com/bazelbuild/bazel/issues/10959
    tags = ["fails_on_windows"],
)

filegroup(
    name = "configs",
    srcs = glob([
        "config/*.yaml",
        "config/*.json",
        "config/*.pb_text",
    ]),
)
