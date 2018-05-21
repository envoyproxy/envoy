licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ws_handler_lib",
    srcs = ["ws_handler_impl.cc"],
    hdrs = ["ws_handler_impl.h"],
    deps = [
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:websocket_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/tcp_proxy",
    ],
)
