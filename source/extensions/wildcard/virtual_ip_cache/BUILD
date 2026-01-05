load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_manager_lib",
    srcs = ["cache_manager.cc"],
    hdrs = ["cache_manager.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cache_manager_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/wildcard/virtual_ip_cache/v3:pkg_cc_proto",
    ],
)
