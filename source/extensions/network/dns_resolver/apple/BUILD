load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = select({
        "//bazel:apple": ["apple_dns_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:apple": ["apple_dns_impl.h"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:dns_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:linked_object",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
