load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# The tool moved to //tools/schema_validator. These are for users of the old labels.

alias(
    name = "schema_validator_tool",
    actual = "//tools/schema_validator:schema_validator_tool",
)

alias(
    name = "schema_validator_tool.stripped",
    actual = "//tools/schema_validator:schema_validator_tool.stripped",
)

alias(
    name = "schema_validator_lib",
    actual = "//tools/schema_validator:schema_validator_lib",
)
