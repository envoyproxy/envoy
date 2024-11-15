load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

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
    external_deps = ["ssl"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/common/tls:stats_lib",
        "//source/common/tls:utility_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/hash",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)
