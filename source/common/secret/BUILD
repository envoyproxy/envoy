load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "secret_manager_impl_lib",
    srcs = ["secret_manager_impl.cc"],
    hdrs = ["secret_manager_impl.h"],
    deps = [
        ":sds_api_lib",
        ":secret_provider_impl_lib",
        "//envoy/secret:secret_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "secret_provider_impl_lib",
    srcs = ["secret_provider_impl.cc"],
    hdrs = ["secret_provider_impl.h"],
    deps = [
        "//envoy/secret:secret_provider_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/config:datasource_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "sds_api_lib",
    srcs = ["sds_api.cc"],
    hdrs = ["sds_api.h"],
    deps = [
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/init:manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/secret:secret_provider_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:utility_lib",
        "//source/common/config:watched_directory_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
