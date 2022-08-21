load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "kv_store_xds_delegate_test",
    srcs = [
        "kv_store_xds_delegate_test.cc",
    ],
    deps = [
        "//contrib/xds/source:kv_store_xds_delegate",
        "//source/common/config:xds_source_id_lib",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "kv_store_xds_delegate_integration_test",
    srcs = [
        "kv_store_xds_delegate_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        "//contrib/xds/source:kv_store_xds_delegate",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)
