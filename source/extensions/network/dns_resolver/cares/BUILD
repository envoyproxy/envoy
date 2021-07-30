load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "dns_lib",
    srcs = ["dns_impl.cc"],
    hdrs = ["dns_impl.h"],
    external_deps = ["ares"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:dns_factory_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/network:resolver_lib",
    ],
)
