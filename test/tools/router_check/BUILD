load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# The tool moved to //tools/router_check. These are for users of the old labels.

alias(
    name = "router_check_tool",
    actual = "//tools/router_check:router_check_tool",
)

alias(
    name = "router_check_tool.stripped",
    actual = "//tools/router_check:router_check_tool.stripped",
)

alias(
    name = "router_check_main_lib",
    actual = "//tools/router_check:router_check_main_lib",
)
