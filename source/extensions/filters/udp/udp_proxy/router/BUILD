load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//source/common/common:empty_string",
        "//source/common/matcher:matcher_lib",
        "//source/common/matcher:validation_visitor_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/network/matching:inputs_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)
