licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_sh_test",
    "envoy_select_hot_restart",
)

envoy_package()

envoy_cc_test_library(
    name = "utility_lib",
    srcs = [
        "utility.cc",
    ],
    hdrs = [
        "utility.h",
    ],
    external_deps = ["envoy_bootstrap"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)
