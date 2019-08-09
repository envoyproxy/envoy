licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/filter/http/dynamic_forward_proxy/v2alpha:dynamic_forward_proxy_cc",
    ],
)

envoy_cc_library(
    name = "proxy_proto_descriptors_lib",
    srcs = ["dynamic_forward_proxy_proto_descriptors.cc"],
    hdrs = ["dynamic_forward_proxy_proto_descriptors.h"],
    deps = [
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/dynamic_forward_proxy/v2alpha:cluster_cc",
        "@envoy_api//envoy/config/filter/http/dynamic_forward_proxy/v2alpha:dynamic_forward_proxy_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/dynamic_forward_proxy:proxy_filter_lib",
        "//source/extensions/filters/http/dynamic_forward_proxy:proxy_proto_descriptors_lib",
    ],
)
