load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "provisional_dispatcher_lib",
    srcs = ["provisional_dispatcher.cc"],
    hdrs = ["provisional_dispatcher.h"],
    external_deps = ["abseil_optional"],
    repository = "@envoy",
    deps = [
        "//library/common/thread:lock_guard_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/event:deferred_deletable",
        "@envoy//envoy/event:dispatcher_interface",
        "@envoy//source/common/common:lock_guard_lib",
        "@envoy//source/common/common:thread_synchronizer_lib",
    ],
)
