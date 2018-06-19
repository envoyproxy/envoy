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
        ":sds_api_lib",
        ":secret_manager_util",
        "//include/envoy/secret:secret_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "secret_manager_util",
    hdrs = ["secret_manager_util.h"],
    deps = [
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)

envoy_cc_library(
    name = "sds_api_lib",
    srcs = ["sds_api.cc"],
    hdrs = ["sds_api.h"],
    deps = [
        ":secret_manager_util",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/config:resources_lib",
        "//source/common/config:subscription_factory_lib",
    ],
)
