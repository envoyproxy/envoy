load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "scope_provider_singleton_lib",
    srcs = ["scope_provider_singleton.cc"],
    hdrs = ["scope_provider_singleton.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/stats:stats_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    deps = [
        ":scope_provider_singleton_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/stats:tag_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/stats:tag_utility_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "//source/extensions/matching/actions/transform_stat:transform_stat_lib",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/stats/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":stats_lib",
        "//envoy/access_log:access_log_config_interface",
        "//envoy/registry",
        "//source/common/config:config_provider_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/access_loggers/stats/v3:pkg_cc_proto",
    ],
)
