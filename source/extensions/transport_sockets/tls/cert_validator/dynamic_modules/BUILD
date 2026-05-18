load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/router:string_accessor_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:statusor_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/tls:stats_lib",
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_validator/dynamic_modules/v3:pkg_cc_proto",
    ],
)
