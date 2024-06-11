load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    tcmalloc_dep = 1,
    deps = [
        "//envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    tcmalloc_dep = 1,
    deps = [
        ":stats_lib",
    ],
)

envoy_cc_library(
    name = "heap_shrinker_lib",
    srcs = ["heap_shrinker.cc"],
    hdrs = ["heap_shrinker.h"],
    deps = [
        ":utils_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/stats:stats_interface",
        "//source/common/stats:symbol_table_lib",
    ],
)
