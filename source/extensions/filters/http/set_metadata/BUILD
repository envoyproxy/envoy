load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "set_metadata_filter_lib",
    srcs = ["set_metadata_filter.cc"],
    hdrs = ["set_metadata_filter.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/set_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/set_metadata:set_metadata_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/set_metadata/v3:pkg_cc_proto",
    ],
)
