load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_state_lib",
    srcs = ["filter_state.cc"],
    hdrs = ["filter_state.h"],
    deps = [
        "//envoy/router:internal_redirect_interface",
        "//envoy/router:string_accessor_interface",
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_state_lib",
        "//envoy/registry",
        "//envoy/router:internal_redirect_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/internal_redirect/filter_state/v3:pkg_cc_proto",
    ],
)
