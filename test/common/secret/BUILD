licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "secret_manager_impl_test",
    srcs = ["secret_manager_impl_test.cc"],
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        "//source/common/secret:sds_api_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//test/mocks/server:server_mocks",
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
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        "//source/common/secret:sds_api_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)
