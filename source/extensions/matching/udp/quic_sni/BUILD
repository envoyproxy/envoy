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
    hdrs = ["quic_sni_input.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry:registry",
        "//source/common/buffer:buffer_lib",
        "@com_github_google_quiche//:quic_core_server_lib",
        "@com_github_google_quiche//:quic_core_framer_lib",
        "@com_github_google_quiche//:quic_core_packets_lib",
        "@com_github_google_quiche//:quic_core_types_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
        "@com_github_google_quiche//:quic_core_error_codes_lib",
        "@envoy_api//envoy/extensions/matching/inputs/quic_sni/v3:pkg_cc_proto",
    ],
)
