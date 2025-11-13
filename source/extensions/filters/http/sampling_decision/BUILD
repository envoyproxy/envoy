load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sampling_decision_filter_lib",
    srcs = ["sampling_decision_filter.cc"],
    hdrs = ["sampling_decision_filter.h"],
    deps = [
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/sampling_decision/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sampling_decision_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/sampling_decision/v3:pkg_cc_proto",
    ],
)
