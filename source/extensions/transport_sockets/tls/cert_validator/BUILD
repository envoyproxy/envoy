load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cert_validator_lib",
    srcs = [
        "default_validator.cc",
        "factory.cc",
        "san_matcher.cc",
        "utility.cc",
    ],
    hdrs = [
        "cert_validator.h",
        "default_validator.h",
        "factory.h",
        "san_matcher.h",
        "utility.h",
    ],
    external_deps = [
        "ssl",
        "abseil_base",
        "abseil_hash",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/transport_sockets/tls:stats_lib",
        "//source/extensions/transport_sockets/tls:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)
