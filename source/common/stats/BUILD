licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats_impl.cc"],
    hdrs = ["stats_impl.h"],
    external_deps = [
        "abseil_optional",
        "libcircllhist",
    ],
    deps = [
        ":stats_options_lib",
        ":tag_extractor_lib",
        ":utility_lib",
        "//include/envoy/common:time_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_library(
    name = "stats_options_lib",
    hdrs = ["stats_options_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "tag_extractor_lib",
    srcs = ["tag_extractor_impl.cc"],
    hdrs = ["tag_extractor_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:perf_annotation_lib",
    ],
)

envoy_cc_library(
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":stats_lib",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
)
