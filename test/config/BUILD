licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_hot_restart",
    "envoy_sh_test",
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
    deps = [
        "//source/common/config:resources_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//test/integration:server_stats_interface",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
        "@envoy_api//envoy/config/transport_socket/capture/v2alpha:capture_cc",
    ],
)
