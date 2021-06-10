load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "hotkey_lib",
    srcs = ["hotkey_impl.cc"],
    hdrs = ["hotkey_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:thread_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/redis_proxy/hotkey/cache:cache_factory_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)
