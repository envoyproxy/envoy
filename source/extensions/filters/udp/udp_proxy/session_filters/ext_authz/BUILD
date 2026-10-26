load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ext_authz_lib",
    srcs = ["ext_authz.cc"],
    hdrs = ["ext_authz.h"],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:logger_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:null_span_lib",
        "//source/extensions/filters/common/ext_authz:ext_authz_grpc_lib",
        "//source/extensions/filters/common/ext_authz:ext_authz_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ext_authz_lib",
        "//envoy/registry",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3:pkg_cc_proto",
    ],
)
