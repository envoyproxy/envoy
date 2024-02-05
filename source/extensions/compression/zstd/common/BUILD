load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "zstd_base_lib",
    srcs = ["base.cc"],
    hdrs = ["base.h"],
    external_deps = ["zstd"],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "zstd_dictionary_manager_lib",
    hdrs = ["dictionary_manager.h"],
    external_deps = ["zstd"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:datasource_lib",
    ],
)
