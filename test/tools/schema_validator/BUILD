licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_binary(
    name = "schema_validator_tool",
    deps = [":schema_validator_lib"],
)

envoy_cc_test_library(
    name = "schema_validator_lib",
    srcs = [
        "schema_validator.cc",
        "validator.cc",
        "validator.h",
    ],
    # TCLAP command line parser needs this to support int64_t/uint64_t in several build environments.
    copts = ["-DHAVE_LONG_LONG"],
    external_deps = ["tclap"],
    deps = [
        "//include/envoy/api:api_interface",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)
