load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    security_posture = "robust_to_untrusted_downstream",
    visibility = ["//visibility:public"],
    deps = [
        ":upstream_request_lib",
        "@envoy_api//envoy/extensions/upstreams/http/http/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_request_lib",
    srcs = [
        "upstream_request.cc",
    ],
    hdrs = [
        "upstream_request.h",
    ],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
    ],
)
