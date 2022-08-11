load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "sgx_private_key_provider_lib",
    srcs = [
        "sgx.cc",
        "sgx_private_key_provider.cc",
        "utility.cc",
    ],
    hdrs = [
        "sgx.h",
        "sgx_private_key_provider.h",
        "utility.h",
        "@pkcs11//:pkcs11headers",
    ],
    copts = ["-I external/pkcs11/include/pkcs11-v2.40"],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = [
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:datasource_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/sgx/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = ["-I external/pkcs11/include/pkcs11-v2.40"],
    visibility = ["//visibility:public"],
    deps = [
        ":sgx_private_key_provider_lib",
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/sgx/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
