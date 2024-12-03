load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "spiffe_validator.cc",
    ],
    hdrs = [
        "spiffe_validator.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/common/tls:stats_lib",
        "//source/common/tls:utility_lib",
        "//source/common/tls/cert_validator:cert_validator_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/hash",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
