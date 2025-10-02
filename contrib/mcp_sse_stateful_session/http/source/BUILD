load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "envelope_lib",
    srcs = [
        "envelope.cc",
    ],
    hdrs = [
        "envelope.h",
    ],
    deps = [
        "//envoy/http:sse_stateful_session_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:base64_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//contrib/envoy/extensions/http/sse_stateful_session/envelope/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":envelope_lib",
        "//envoy/http:sse_stateful_session_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//contrib/envoy/extensions/http/sse_stateful_session/envelope/v3alpha:pkg_cc_proto",
    ],
)
