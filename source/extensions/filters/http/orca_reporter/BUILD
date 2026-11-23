load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# ORCA load report L7 HTTP filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/orca_reporter_filter

envoy_extension_package()

envoy_cc_library(
    name = "cgroup_stats_lib",
    srcs = ["cgroup_stats.cc"],
    hdrs = ["cgroup_stats.h"],
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:fmt_lib",
        "//source/common/matcher:regex_replace_lib",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

envoy_cc_library(
    name = "orca_reporter_lib",
    srcs = ["orca_reporter.cc"],
    hdrs = ["orca_reporter.h"],
    deps = [
        ":cgroup_stats_lib",
        "//envoy/common:regex_interface",
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/orca:orca_parser",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@envoy_api//envoy/extensions/filters/http/orca_reporter/v3:pkg_cc_proto",
        "@xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":orca_reporter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/orca_reporter/v3:pkg_cc_proto",
    ],
)
