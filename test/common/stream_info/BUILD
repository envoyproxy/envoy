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
        "//source/common/stream_info:filter_state_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stream_info_impl_test",
    srcs = ["stream_info_impl_test.cc"],
    deps = [
        ":test_int_accessor_lib",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:host_description_interface",
        "//source/common/stream_info:stream_info_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test_library(
    name = "test_int_accessor_lib",
    hdrs = ["test_int_accessor.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_test_library(
    name = "test_util",
    hdrs = ["test_util.h"],
    deps = [
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/stream_info:filter_state_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/stream_info:utility_lib",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_test(
    name = "uint32_accessor_impl_test",
    srcs = ["uint32_accessor_impl_test.cc"],
    deps = [
        "//source/common/stream_info:uint32_accessor_lib",
    ],
)
