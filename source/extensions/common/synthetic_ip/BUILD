load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_lib",
    srcs = ["cache.cc"],
    hdrs = ["cache.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "cache_manager_lib",
    srcs = ["cache_manager.cc"],
    hdrs = ["cache_manager.h"],
    deps = [
        ":cache_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/singleton:instance_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "@com_google_absl//absl/types:optional",
    ],
)
