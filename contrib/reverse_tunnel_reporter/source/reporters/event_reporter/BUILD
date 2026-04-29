load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "event_reporter_lib",
    srcs = [
        "factory.cc",
        "reporter.cc",
    ],
    hdrs = [
        "factory.h",
        "reporter.h",
    ],
    deps = [
        "//contrib/reverse_tunnel_reporter/source:reverse_tunnel_event_types",
        "//envoy/extensions/bootstrap/reverse_tunnel:reverse_tunnel_reporter_lib",
        "//envoy/registry",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters:pkg_cc_proto",
    ],
)
