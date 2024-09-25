load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "subscription_factory_impl_test",
    srcs = ["subscription_factory_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:xds_resource_lib",
        "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//source/extensions/config_subscription/rest:http_subscription_lib",
        "//test/config:v2_link_hacks",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "subscription_impl_test",
    srcs = ["subscription_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//test/common/config:grpc_subscription_test_harness",
        "//test/common/config:subscription_test_harness",
        "//test/extensions/config_subscription/filesystem:filesystem_subscription_test_harness",
        "//test/extensions/config_subscription/grpc:delta_subscription_test_harness",
        "//test/extensions/config_subscription/rest:http_subscription_test_harness",
    ],
)
