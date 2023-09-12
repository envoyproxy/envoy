load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "json_internal_lib",
    srcs = ["json_internal.cc"],
    hdrs = ["json_internal.h"],
    external_deps = [
        "json",
    ],
    deps = [
        "//envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "json_loader_lib",
    srcs = ["json_loader.cc"],
    hdrs = ["json_loader.h"],
    deps = [
        ":json_internal_lib",
        "//envoy/json:json_object_interface",
        "//source/common/runtime:runtime_features_lib",
    ],
)

envoy_cc_library(
    name = "json_sanitizer_lib",
    srcs = ["json_sanitizer.cc"],
    hdrs = ["json_sanitizer.h"],
    deps = [
        ":json_internal_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "json_streamer_lib",
    srcs = ["json_streamer.cc"],
    hdrs = ["json_streamer.h"],
    deps = [
        ":json_sanitizer_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_util_lib",
        "//source/common/common:assert_lib",
    ],
)
