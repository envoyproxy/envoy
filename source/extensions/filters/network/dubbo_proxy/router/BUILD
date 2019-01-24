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

envoy_cc_library(
    name = "router_matcher",
    srcs = ["route_matcher.cc"],
    hdrs = ["route_matcher.h"],
    deps = [
        ":router_interface",
        "//include/envoy/router:router_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "@envoy_api//envoy/config/filter/network/dubbo_proxy/v2alpha1:dubbo_proxy_cc",
    ],
)
