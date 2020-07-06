load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that dynamically adjusts the number of allowed concurrent
# requests based on sampled latencies.
# Public docs: TODO (tonya11en)

envoy_package()

envoy_cc_library(
    name = "controller_lib",
    srcs = ["gradient_controller.cc"],
    hdrs = [
        "controller.h",
        "gradient_controller.h",
    ],
    external_deps = [
        "libcircllhist",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//source/common/event:dispatcher_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "@envoy_api//envoy/extensions/filters/http/adaptive_concurrency/v3:pkg_cc_proto",
    ],
)
