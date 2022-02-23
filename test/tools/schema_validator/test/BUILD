load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_sh_test(
    name = "schema_validator_test",
    srcs = ["schema_validator_tests.sh"],
    cc_binary = ["//test/tools/schema_validator:schema_validator_tool"],
    data = [
        ":configs",
    ],
)

filegroup(
    name = "configs",
    srcs = glob([
        "config/*.yaml",
    ]),
)
