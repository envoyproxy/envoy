load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_certificate_provider",
    srcs = ["local_certificate.cc"],
    hdrs = ["local_certificate.h"],
    external_deps = [
        "simple_lru_cache_lib",
    ],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/certificate_provider:certificate_provider_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/extensions/certificate_providers/local_certificate/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":local_certificate_provider",
        "//envoy/certificate_provider:certificate_provider_factory_lib",
        "//envoy/certificate_provider:certificate_provider_interface",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/extensions/certificate_providers/local_certificate/v3:pkg_cc_proto",
    ],
)
