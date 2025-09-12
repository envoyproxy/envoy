load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "mcp_sse_stateful_session_lib",
    srcs = ["mcp_sse_stateful_session.cc"],
    hdrs = ["mcp_sse_stateful_session.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/http:sse_stateful_session_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/mcp_sse_stateful_session/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//contrib/mcp_sse_stateful_session/filters/http/source:mcp_sse_stateful_session_lib",
        "//envoy/http:sse_stateful_session_interface",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/mcp_sse_stateful_session/v3alpha:pkg_cc_proto",
    ],
)
