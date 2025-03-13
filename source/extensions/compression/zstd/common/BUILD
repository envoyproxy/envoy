load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "zstd_dictionary_manager_lib",
    hdrs = ["dictionary_manager.h"],
    deps = [
        "//bazel/foreign_cc:zstd",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:datasource_lib",
    ],
)
