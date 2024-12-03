load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "runtime_interface",
    hdrs = ["runtime.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/common:assert_lib",
        "//source/common/singleton:threadsafe_singleton",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
