load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "new_grpc_mux_impl_test",
    srcs = ["new_grpc_mux_impl_test.cc"],
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config/xds_mux:grpc_mux_lib",
        "//source/common/protobuf",
        "//source/extensions/config_subscription/grpc:new_grpc_mux_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/config:v2_link_hacks",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/config:custom_config_validators_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "grpc_mux_impl_test",
    srcs = ["grpc_mux_impl_test.cc"],
    deps = [
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//source/common/config:api_version_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/config_subscription/grpc:grpc_mux_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/config:custom_config_validators_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "delta_subscription_impl_test",
    srcs = ["delta_subscription_impl_test.cc"],
    deps = [
        ":delta_subscription_test_harness",
        "//envoy/config:xds_config_tracker_interface",
        "//source/common/config:api_version_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "delta_subscription_state_test",
    srcs = ["delta_subscription_state_test.cc"],
    deps = [
        "//source/common/config/xds_mux:delta_subscription_state_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/config_subscription/grpc:delta_subscription_state_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "delta_subscription_state_old_test",
    srcs = ["delta_subscription_state_old_test.cc"],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/config_subscription/grpc:delta_subscription_state_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "delta_subscription_test_harness",
    hdrs = ["delta_subscription_test_harness.h"],
    deps = [
        "//envoy/config:xds_config_tracker_interface",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//test/common/config:subscription_test_harness",
        "//test/mocks/config:config_mocks",
        "//test/mocks/config:custom_config_validators_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
