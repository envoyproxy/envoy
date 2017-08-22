licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_binary(
    name = "schema_validator_tool",
    testonly = 1,
    deps = [":schema_validator_lib"],
)

envoy_cc_test_library(
    name = "schema_validator_lib",
    srcs = [
        "schema_validator.cc",
        "validator.cc",
        "validator.h",
    ],
    external_deps = ["tclap"],
    deps = [
        "//include/envoy/common:base_includes",
        "//source/common/config:rds_json_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:config_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:printers_lib",
    ],
)
