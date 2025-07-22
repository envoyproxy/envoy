load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "mcp_sse_lib",
    srcs = [
        "mcp_sse.cc",
        "mcp_sse.h",
        "mcp_sse_stateful_session.h",
    ],
    hdrs = [
        "mcp_sse.h",
        "mcp_sse_stateful_session.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:stateful_session_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:base64_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//contrib/envoy/extensions/http/stateful_session/mcp_sse/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mcp_sse_lib",
        "//envoy/http:stateful_session_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//contrib/envoy/extensions/http/stateful_session/mcp_sse/v3:pkg_cc_proto",
    ],
)
