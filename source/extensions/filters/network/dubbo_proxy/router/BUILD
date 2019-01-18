licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/router:router_interface",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)
