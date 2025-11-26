load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//envoy/singleton:manager_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/connection_pool_cardinality/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        "//envoy/common:hashable_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)
