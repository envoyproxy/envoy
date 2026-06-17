load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "http_formatter_context_interface",
    hdrs = [
        "http_formatter_context.h",
    ],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/tracing:null_span_lib",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "substitution_formatter_interface",
    hdrs = [
        "substitution_formatter.h",
    ],
    deps = [
        ":http_formatter_context_interface",
        "//envoy/common:pure_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/protobuf",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)
