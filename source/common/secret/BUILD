licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_manager_impl_lib",
    srcs = ["secret_manager_impl.cc"],
    hdrs = ["secret_manager_impl.h"],
    deps = [
        ":secret_provider_impl_lib",
        "//include/envoy/secret:secret_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "secret_provider_impl_lib",
    srcs = ["secret_provider_impl.cc"],
    hdrs = ["secret_provider_impl.h"],
    deps = [
        "//include/envoy/secret:secret_provider_interface",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "sds_api_lib",
    srcs = ["sds_api.cc"],
    hdrs = ["sds_api.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_provider_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/config:resources_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
    ],
)
