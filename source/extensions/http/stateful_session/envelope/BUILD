load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "envelope_lib",
    srcs = ["envelope.cc"],
    hdrs = ["envelope.h"],
    deps = [
        "//envoy/http:stateful_session_interface",
        "//source/common/common:base64_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/extensions/http/stateful_session/envelope/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":envelope_lib",
        "//envoy/http:stateful_session_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/stateful_session/envelope/v3:pkg_cc_proto",
    ],
)
