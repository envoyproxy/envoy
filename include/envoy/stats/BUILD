load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "refcount_ptr_interface",
    hdrs = ["refcount_ptr.h"],
    deps = ["//source/common/common:assert_lib"],
)

# TODO(jmarantz): atomize the build rules to match the include files.
envoy_cc_library(
    name = "stats_interface",
    hdrs = [
        "allocator.h",
        "histogram.h",
        "scope.h",
        "sink.h",
        "stats.h",
        "stats_matcher.h",
        "store.h",
        "tag.h",
        "tag_extractor.h",
        "tag_producer.h",
    ],
    external_deps = ["abseil_inlined_vector"],
    deps = [
        ":refcount_ptr_interface",
        ":symbol_table_interface",
        "//include/envoy/common:interval_set_interface",
    ],
)

envoy_cc_library(
    name = "primitive_stats_interface",
    hdrs = [
        "primitive_stats.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
    ],
)

envoy_cc_library(
    name = "symbol_table_interface",
    hdrs = ["symbol_table.h"],
    external_deps = ["abseil_inlined_vector"],
    deps = [
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_library(
    name = "timespan_interface",
    hdrs = ["timespan.h"],
)

envoy_cc_library(
    name = "stats_macros",
    hdrs = ["stats_macros.h"],
    deps = [":stats_interface"],
)

envoy_cc_library(
    name = "primitive_stats_macros",
    hdrs = ["primitive_stats_macros.h"],
    deps = [":primitive_stats_interface"],
)
