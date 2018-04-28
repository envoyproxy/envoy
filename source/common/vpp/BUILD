licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "vpp_socket_lib",
    srcs = ["vpp_socket.cc"],
    hdrs = ["vpp_socket.h"],
    external_deps = ["vppcom"],
    deps = [
        ":vpp_buffer_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/common:non_copyable",
        "//source/common/event:libevent_lib",
        "//source/common/http:headers_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/event:dispatcher_includes",
    ],
)

envoy_cc_library(
    name = "vpp_buffer_lib",
    srcs = ["vpp_buffer.cc"],
    hdrs = ["vpp_buffer.h"],
    external_deps = ["vppcom"],
    deps = [
        ":vpp_connection_includes",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/common:non_copyable",
        "//source/common/event:libevent_lib",
        "//source/common/http:headers_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/event:dispatcher_includes",
    ],
)
envoy_cc_library(
    name = "vpp_connection_lib",
    srcs = ["vpp_connection_impl.cc"],
    hdrs = ["vpp_connection_impl.h"],
    deps = [
        ":vpp_socket_lib",
        "//source/common/network:address_lib",
        "//source/common/network:filter_manager_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:logger_lib",
        "//source/common/event:libevent_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/ssl:ssl_socket_lib",
    ],
)

envoy_cc_library(
    name = "vpp_connection_includes",
    hdrs = [
        "vpp_connection_impl.h",
    ],
    deps = [
        "//source/common/network:filter_manager_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/ssl:ssl_socket_lib",
    ],
)

