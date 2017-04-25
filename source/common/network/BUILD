load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "address_lib",
    srcs = ["address_impl.cc"],
    hdrs = ["address_impl.h"],
    deps = [
        "//include/envoy/network:address_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "cidr_range_lib",
    srcs = ["cidr_range.cc"],
    hdrs = ["cidr_range.h"],
    deps = [
        ":address_lib",
        ":utility_lib",
        "//include/envoy/network:address_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "connection_lib",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
    deps = [
        ":address_lib",
        ":filter_manager_lib",
        ":utility_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:logger_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:libevent_lib",
    ],
)

envoy_cc_library(
    name = "dns_lib",
    srcs = ["dns_impl.cc"],
    hdrs = ["dns_impl.h"],
    external_deps = ["ares"],
    deps = [
        ":address_lib",
        ":utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:dns_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    hdrs = ["filter_impl.h"],
)

envoy_cc_library(
    name = "filter_manager_lib",
    srcs = ["filter_manager_impl.cc"],
    hdrs = ["filter_manager_impl.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
    ],
)

envoy_cc_library(
    name = "listen_socket_lib",
    srcs = ["listen_socket_impl.cc"],
    hdrs = ["listen_socket_impl.h"],
    deps = [
        ":address_lib",
        ":utility_lib",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/ssl:context_lib",
    ],
)

envoy_cc_library(
    name = "listener_lib",
    srcs = [
        "listener_impl.cc",
        "proxy_protocol.cc",
    ],
    hdrs = [
        "listener_impl.h",
        "proxy_protocol.h",
    ],
    deps = [
        ":address_lib",
        ":connection_lib",
        ":listen_socket_lib",
        ":utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:connection_handler_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:libevent_lib",
        "//source/common/ssl:connection_lib",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":address_lib",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)
