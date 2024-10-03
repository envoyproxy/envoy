load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "private_key_provider_proto",
    srcs = ["private_key_provider.proto"],
)

envoy_cc_test(
    name = "secret_manager_impl_test",
    srcs = ["secret_manager_impl_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":private_key_provider_proto_cc_proto",
        "//source/common/secret:sds_api_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:config_tracker_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/grpc_credential/v2alpha:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_api_test",
    srcs = ["sds_api_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/secret:sds_api_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)
