licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "filter_state_impl_test",
    srcs = ["filter_state_impl_test.cc"],
    deps = [
        "//source/common/request_info:filter_state_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "request_info_impl_test",
    srcs = ["request_info_impl_test.cc"],
    deps = [
        ":test_int_accessor_lib",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:host_description_interface",
        "//source/common/request_info:request_info_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test_library(
    name = "test_int_accessor_lib",
    hdrs = ["test_int_accessor.h"],
    deps = [
        "//include/envoy/request_info:filter_state_interface",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/request_info:utility_lib",
        "//test/mocks/request_info:request_info_mocks",
    ],
)
