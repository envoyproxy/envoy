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
    name = "api_shadow_test",
    srcs = ["api_shadow_test.cc"],
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg_cc_proto"],
)

envoy_cc_test(
    name = "api_type_oracle_test",
    srcs = ["api_type_oracle_test.cc"],
    deps = [
        "//source/common/config:api_type_oracle_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
    ],
)

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
    name = "delta_subscription_impl_test",
    srcs = ["delta_subscription_impl_test.cc"],
    deps = [
        ":delta_subscription_test_harness",
        "//source/common/config:api_version_lib",
        "//source/common/config:grpc_subscription_lib",
        "//source/common/config:new_grpc_mux_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "delta_subscription_state_test",
    srcs = ["delta_subscription_state_test.cc"],
    deps = [
        "//source/common/config:delta_subscription_state_lib",
        "//source/common/config:grpc_subscription_lib",
        "//source/common/config:new_grpc_mux_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filesystem_subscription_impl_test",
    srcs = ["filesystem_subscription_impl_test.cc"],
    deps = [
        ":filesystem_subscription_test_harness",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "filesystem_subscription_test_harness",
    srcs = ["filesystem_subscription_test_harness.h"],
    deps = [
        ":subscription_test_harness",
        "//source/common/config:filesystem_subscription_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "grpc_mux_impl_test",
    srcs = ["grpc_mux_impl_test.cc"],
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/config:grpc_mux_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:version_converter_lib",
        "//source/common/protobuf",
        "//source/common/stats:isolated_store_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "new_grpc_mux_impl_test",
    srcs = ["new_grpc_mux_impl_test.cc"],
    deps = [
        "//source/common/config:new_grpc_mux_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:version_converter_lib",
        "//source/common/protobuf",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
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
    name = "grpc_stream_test",
    srcs = ["grpc_stream_test.cc"],
    deps = [
        "//source/common/config:grpc_stream_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
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
        "//source/common/common:hash_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:grpc_mux_lib",
        "//source/common/config:grpc_subscription_lib",
        "//source/common/config:version_converter_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "delta_subscription_test_harness",
    hdrs = ["delta_subscription_test_harness.h"],
    deps = [
        ":subscription_test_harness",
        "//source/common/config:new_grpc_mux_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/grpc:common_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_subscription_impl_test",
    srcs = ["http_subscription_impl_test.cc"],
    deps = [
        ":http_subscription_test_harness",
    ],
)

envoy_cc_test_library(
    name = "http_subscription_test_harness",
    srcs = ["http_subscription_test_harness.h"],
    deps = [
        ":subscription_test_harness",
        "//include/envoy/http:async_client_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:http_subscription_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
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

envoy_cc_test(
    name = "subscription_factory_impl_test",
    srcs = ["subscription_factory_impl_test.cc"],
    deps = [
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:udpa_resource_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "subscription_impl_test",
    srcs = ["subscription_impl_test.cc"],
    deps = [
        ":delta_subscription_test_harness",
        ":filesystem_subscription_test_harness",
        ":grpc_subscription_test_harness",
        ":http_subscription_test_harness",
        ":subscription_test_harness",
    ],
)

envoy_cc_test_library(
    name = "subscription_test_harness",
    srcs = ["subscription_test_harness.h"],
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
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "pausable_ack_queue_test",
    srcs = ["pausable_ack_queue_test.cc"],
    deps = [
        "//source/common/config:pausable_ack_queue_lib",
    ],
)

envoy_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    deps = [
        "//include/envoy/common:base_includes",
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
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/stats:stats_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_udpa//udpa/type/v1:pkg_cc_proto",
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
        "//include/envoy/config:typed_config_interface",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "watch_map_test",
    srcs = ["watch_map_test.cc"],
    deps = [
        "//source/common/config:watch_map_lib",
        "//test/mocks/config:config_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
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
    name = "datasource_test",
    srcs = ["datasource_test.cc"],
    deps = [
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/http:message_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "udpa_context_params_test",
    srcs = ["udpa_context_params_test.cc"],
    deps = [
        ":udpa_test_utility_lib",
        "//source/common/config:udpa_context_params_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "udpa_resource_test",
    srcs = ["udpa_resource_test.cc"],
    deps = [
        ":udpa_test_utility_lib",
        "//source/common/config:udpa_resource_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "udpa_test_utility_lib",
    hdrs = ["udpa_test_utility.h"],
)

envoy_proto_library(
    name = "version_converter_proto",
    srcs = ["version_converter.proto"],
)

envoy_cc_test(
    name = "version_converter_test",
    srcs = ["version_converter_test.cc"],
    deps = [
        ":version_converter_proto_cc_proto",
        "//source/common/config:api_version_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/protobuf:well_known_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
