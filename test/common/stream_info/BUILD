load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

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
        "//envoy/http:protocol_interface",
        "//envoy/upstream:host_description_interface",
        "//source/common/stream_info:stream_id_provider_lib",
        "//source/common/stream_info:stream_info_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "test_int_accessor_lib",
    hdrs = ["test_int_accessor.h"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_test_library(
    name = "test_util",
    hdrs = ["test_util.h"],
    deps = [
        "//envoy/http:request_id_extension_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/common/stream_info:stream_id_provider_lib",
        "//source/extensions/request_id/uuid:config",
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
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "uint32_accessor_impl_test",
    srcs = ["uint32_accessor_impl_test.cc"],
    deps = [
        "//source/common/stream_info:uint32_accessor_lib",
    ],
)

envoy_cc_test(
    name = "uint64_accessor_impl_test",
    srcs = ["uint64_accessor_impl_test.cc"],
    deps = [
        "//source/common/stream_info:uint64_accessor_lib",
    ],
)

envoy_cc_test(
    name = "bool_accessor_impl_test",
    srcs = ["bool_accessor_impl_test.cc"],
    deps = [
        "//source/common/stream_info:bool_accessor_lib",
    ],
)
