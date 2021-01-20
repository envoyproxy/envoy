load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "address_lib",
    srcs = ["address_impl.cc"],
    hdrs = ["address_impl.h"],
    deps = [
        ":socket_interface_lib",
        "//include/envoy/network:address_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "application_protocol_lib",
    srcs = ["application_protocol.cc"],
    hdrs = ["application_protocol.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_balancer_lib",
    srcs = ["connection_balancer_impl.cc"],
    hdrs = ["connection_balancer_impl.h"],
    deps = [
        "//include/envoy/network:connection_balancer_interface",
    ],
)

envoy_cc_library(
    name = "connection_base_lib",
    srcs = ["connection_impl_base.cc"],
    hdrs = ["connection_impl_base.h"],
    deps = [
        ":filter_manager_lib",
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
    ],
)

envoy_cc_library(
    name = "connection_lib",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":address_lib",
        ":connection_base_lib",
        ":raw_buffer_socket_lib",
        ":utility_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server/overload:thread_local_overload_state",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/event:libevent_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/stream_info:stream_info_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "apple_dns_lib",
    srcs = select({
        "//bazel:apple": ["apple_dns_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:apple": ["apple_dns_impl.h"],
        "//conditions:default": [],
    }),
    deps = [
        ":address_lib",
        ":utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:dns_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:linked_object",
        "//source/common/singleton:threadsafe_singleton",
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
    deps = [
        "//include/envoy/network:filter_interface",
    ],
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
    name = "hash_policy_lib",
    srcs = ["hash_policy.cc"],
    hdrs = ["hash_policy.h"],
    deps = [
        "//include/envoy/network:hash_policy_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "io_socket_error_lib",
    srcs = ["io_socket_error_impl.cc"],
    hdrs = ["io_socket_error_impl.h"],
    deps = [
        "//include/envoy/api:io_error_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "lc_trie_lib",
    hdrs = ["lc_trie.h"],
    external_deps = [
        "abseil_node_hash_set",
        "abseil_int128",
    ],
    deps = [
        ":address_lib",
        ":cidr_range_lib",
        ":utility_lib",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "socket_interface_lib",
    hdrs = ["socket_interface.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/network:socket_interface_interface",
        "//include/envoy/registry",
        "//include/envoy/server:bootstrap_extension_config_interface",
    ],
)

envoy_cc_library(
    name = "default_socket_interface_lib",
    srcs = [
        "io_socket_handle_impl.cc",
        "socket_interface_impl.cc",
    ],
    hdrs = [
        "io_socket_handle_impl.h",
        "socket_interface_impl.h",
    ],
    deps = [
        ":address_lib",
        ":io_socket_error_lib",
        ":socket_interface_lib",
        ":socket_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:io_handle_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/event:dispatcher_includes",
        "@envoy_api//envoy/extensions/network/socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_lib",
    srcs = ["socket_impl.cc"],
    hdrs = ["socket_impl.h"],
    deps = [
        "//include/envoy/network:socket_interface",
        "//include/envoy/network:socket_interface_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "listen_socket_lib",
    srcs = ["listen_socket_impl.cc"],
    hdrs = ["listen_socket_impl.h"],
    deps = [
        ":socket_lib",
        ":utility_lib",
        "//include/envoy/network:exception_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_lib",
    srcs = [
        "base_listener_impl.cc",
        "tcp_listener_impl.cc",
        "udp_listener_impl.cc",
    ],
    hdrs = [
        "base_listener_impl.h",
        "tcp_listener_impl.h",
        "udp_listener_impl.h",
    ],
    deps = [
        ":address_lib",
        ":default_socket_interface_lib",
        ":listen_socket_lib",
        ":udp_default_writer_config",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:exception_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/event:dispatcher_includes",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "raw_buffer_socket_lib",
    srcs = ["raw_buffer_socket.cc"],
    hdrs = ["raw_buffer_socket.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:headers_lib",
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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_option_lib",
    srcs = ["socket_option_impl.cc"],
    hdrs = ["socket_option_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":address_lib",
        ":listen_socket_lib",
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "addr_family_aware_socket_option_lib",
    srcs = ["addr_family_aware_socket_option_impl.cc"],
    hdrs = ["addr_family_aware_socket_option_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":address_lib",
        ":socket_lib",
        ":socket_option_lib",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_option_factory_lib",
    srcs = ["socket_option_factory.cc"],
    hdrs = ["socket_option_factory.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":addr_family_aware_socket_option_lib",
        ":address_lib",
        ":socket_option_lib",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:logger_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":address_lib",
        ":default_socket_interface_lib",
        ":socket_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "transport_socket_options_lib",
    srcs = ["transport_socket_options_impl.cc"],
    hdrs = ["transport_socket_options_impl.h"],
    deps = [
        ":application_protocol_lib",
        ":proxy_protocol_filter_state_lib",
        ":upstream_server_name_lib",
        ":upstream_subject_alt_names_lib",
        "//include/envoy/network:proxy_protocol_options_lib",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "upstream_server_name_lib",
    srcs = ["upstream_server_name.cc"],
    hdrs = ["upstream_server_name.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "upstream_subject_alt_names_lib",
    srcs = ["upstream_subject_alt_names.cc"],
    hdrs = ["upstream_subject_alt_names.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "filter_matcher_lib",
    srcs = [
        "filter_matcher.cc",
    ],
    hdrs = ["filter_matcher.h"],
    external_deps = [
        "abseil_str_format",
    ],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "udp_packet_writer_handler_lib",
    srcs = ["udp_packet_writer_handler_impl.cc"],
    hdrs = ["udp_packet_writer_handler_impl.h"],
    deps = [
        ":io_socket_error_lib",
        ":utility_lib",
        "//include/envoy/network:socket_interface",
        "//include/envoy/network:udp_packet_writer_config_interface",
        "//include/envoy/network:udp_packet_writer_handler_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "udp_default_writer_config",
    srcs = ["udp_default_writer_config.cc"],
    hdrs = ["udp_default_writer_config.h"],
    deps = [
        ":udp_packet_writer_handler_lib",
        "//include/envoy/network:udp_packet_writer_config_interface",
        "//include/envoy/registry",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "proxy_protocol_filter_state_lib",
    srcs = ["proxy_protocol_filter_state.cc"],
    hdrs = ["proxy_protocol_filter_state.h"],
    deps = [
        "//include/envoy/network:proxy_protocol_options_lib",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)
