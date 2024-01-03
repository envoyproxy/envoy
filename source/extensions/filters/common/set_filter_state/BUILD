load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_config_lib",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "@envoy_api//envoy/extensions/filters/common/set_filter_state/v3:pkg_cc_proto",
    ],
)
