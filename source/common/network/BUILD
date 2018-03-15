licenses(["notice"])  # Apache 2

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
        "//include/envoy/json:json_object_interface",
        "//include/envoy/network:address_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@envoy_api//envoy/api/v2/core:address_cc",
    ],
)

envoy_cc_library(
    name = "connection_lib",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":address_lib",
        ":filter_manager_lib",
        ":raw_buffer_socket_lib",
        ":utility_lib",
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
    name = "lc_trie_lib",
    srcs = ["lc_trie.cc"],
    hdrs = ["lc_trie.h"],
    external_deps = ["abseil_int128"],
    deps = [
        ":address_lib",
        ":cidr_range_lib",
        ":utility_lib",
        "//source/common/common:assert_lib",
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
    ],
    hdrs = [
        "listener_impl.h",
    ],
    deps = [
        ":address_lib",
        ":listen_socket_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:libevent_lib",
    ],
)

envoy_cc_library(
    name = "raw_buffer_socket_lib",
    srcs = ["raw_buffer_socket.cc"],
    hdrs = ["raw_buffer_socket.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:headers_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "resolver_lib",
    srcs = ["resolver_impl.cc"],
    hdrs = ["resolver_impl.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:resolver_interface",
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/network:address_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":address_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/core:address_cc",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)
