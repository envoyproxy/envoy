load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":upstream_request_lib",
        "@envoy_api//envoy/extensions/upstreams/http/udp/v3:pkg_cc_proto",
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
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "@com_github_google_quiche//:quiche_common_capsule_lib",
        "@com_github_google_quiche//:quiche_common_connect_udp_datagram_payload_lib",
    ],
)
