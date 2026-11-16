load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# The tool moved to //tools/config_load_check. These are for users of the old labels.

alias(
    name = "config_load_check_tool",
    actual = "//tools/config_load_check:config_load_check_tool",
)

alias(
    name = "config_load_check_tool.stripped",
    actual = "//tools/config_load_check:config_load_check_tool.stripped",
)

alias(
    name = "config_load_check_lib",
    actual = "//tools/config_load_check:config_load_check_lib",
)
