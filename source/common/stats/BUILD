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
        "//include/envoy/common:time_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
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
