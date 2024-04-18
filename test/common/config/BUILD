load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "decoded_resource_impl_test",
    srcs = ["decoded_resource_impl_test.cc"],
    deps = [
        "//source/common/config:decoded_resource_lib",
        "//test/mocks/config:config_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ttl_test",
    srcs = ["ttl_test.cc"],
    deps = [
        "//source/common/config:ttl_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "grpc_subscription_impl_test",
    srcs = ["grpc_subscription_impl_test.cc"],
    deps = [
        ":grpc_subscription_test_harness",
        "//source/common/buffer:zero_copy_input_stream_lib",
    ],
)

envoy_cc_test_library(
    name = "grpc_subscription_test_harness",
    hdrs = ["grpc_subscription_test_harness.h"],
    deps = [
        ":subscription_test_harness",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//source/common/common:hash_lib",
        "//source/common/config:api_version_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/config:custom_config_validators_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "opaque_resource_decoder_impl_test",
    srcs = ["opaque_resource_decoder_impl_test.cc"],
    deps = [
        "//source/common/config:opaque_resource_decoder_lib",
        "//source/common/protobuf:message_validator_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "subscription_test_harness",
    hdrs = ["subscription_test_harness.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "type_to_endpoint_test",
    srcs = ["type_to_endpoint_test.cc"],
    deps = [
        "//source/common/config:type_to_endpoint_lib",
        "//test/config:v2_link_hacks",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    deps = [
        "//envoy/common:base_includes",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "runtime_utility_test",
    srcs = ["runtime_utility_test.cc"],
    deps = [
        "//source/common/config:runtime_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/stats:stats_lib",
        "//source/extensions/access_loggers/file:config",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//udpa/type/v1:pkg_cc_proto",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/cors/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "registry_test",
    srcs = ["registry_test.cc"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "dummy_config_proto",
    srcs = ["dummy_config.proto"],
)

envoy_cc_test(
    name = "config_provider_impl_test",
    srcs = ["config_provider_impl_test.cc"],
    deps = [
        ":dummy_config_proto_cc_proto",
        "//source/common/config:config_provider_lib",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "context_provider_impl_test",
    srcs = ["context_provider_impl_test.cc"],
    deps = [
        ":xds_test_utility_lib",
        "//source/common/config:context_provider_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "datasource_test",
    srcs = ["datasource_test.cc"],
    deps = [
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xds_context_params_test",
    srcs = ["xds_context_params_test.cc"],
    deps = [
        ":xds_test_utility_lib",
        "//source/common/config:xds_context_params_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "xds_resource_test",
    srcs = ["xds_resource_test.cc"],
    deps = [
        ":xds_test_utility_lib",
        "//source/common/config:xds_resource_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "xds_test_utility_lib",
    hdrs = ["xds_test_utility.h"],
)

envoy_cc_test(
    name = "watched_directory_test",
    srcs = ["watched_directory_test.cc"],
    deps = [
        "//source/common/config:watched_directory_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
    ],
)

envoy_cc_test(
    name = "custom_config_validators_impl_test",
    srcs = ["custom_config_validators_impl_test.cc"],
    deps = [
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
    ],
)
