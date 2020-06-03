licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
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
        "//include/envoy/http:filter_interface",
        "//include/envoy/upstream:upstream_interface",
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
    ],
)

envoy_cc_test(
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    deps = [
        ":upstream_request_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
