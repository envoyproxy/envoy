load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "substitution_formatter_interface",
    hdrs = [
        "substitution_formatter.h",
        "substitution_formatter_base.h",
    ],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/http:header_map_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)
