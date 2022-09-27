load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
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
        "//envoy/network:address_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "application_protocol_lib",
    srcs = ["application_protocol.cc"],
    hdrs = ["application_protocol.h"],
    deps = [
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "cidr_range_interface",
    hdrs = ["cidr_range.h"],
    deps = [
        "//envoy/network:address_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cidr_range_lib",
    srcs = ["cidr_range.cc"],
    hdrs = ["cidr_range.h"],
    deps = [
        ":address_lib",
        ":utility_lib",
        "//envoy/network:address_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_balancer_lib",
    srcs = ["connection_balancer_impl.cc"],
    hdrs = ["connection_balancer_impl.h"],
    deps = [
        "//envoy/network:connection_balancer_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_base_lib",
    srcs = ["connection_impl_base.cc"],
    hdrs = ["connection_impl_base.h"],
    deps = [
        ":connection_socket_lib",
        ":filter_manager_lib",
        "//envoy/common:scope_tracker_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
    ],
)

envoy_cc_library(
    name = "connection_lib",
    deps = [
        ":connection_impl",
        ":default_client_connection_factory",
    ],
)

envoy_cc_library(
    name = "connection_impl",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
    deps = [
        ":address_lib",
        ":connection_base_lib",
        ":raw_buffer_socket_lib",
        ":utility_lib",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:socket_interface",
        "//envoy/server/overload:thread_local_overload_state",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/event:libevent_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/stream_info:stream_info_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "happy_eyeballs_connection_impl_lib",
    srcs = ["happy_eyeballs_connection_impl.cc"],
    hdrs = ["happy_eyeballs_connection_impl.h"],
    deps = [
        ":connection_base_lib",
        ":connection_lib",
        ":multi_connection_base_impl_lib",
        "//envoy/upstream:upstream_interface",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "multi_connection_base_impl_lib",
    srcs = ["multi_connection_base_impl.cc"],
    hdrs = ["multi_connection_base_impl.h"],
    deps = [
        ":connection_base_lib",
        ":connection_lib",
    ],
)

envoy_cc_library(
    name = "default_client_connection_factory",
    srcs = [
        "default_client_connection_factory.cc",
    ],
    hdrs = [
        "default_client_connection_factory.h",
    ],
    deps = [
        ":address_lib",
        ":connection_base_lib",
        ":connection_impl",
        "//envoy/network:client_connection_factory",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "filter_lib",
    hdrs = ["filter_impl.h"],
    deps = [
        "//envoy/network:filter_interface",
    ],
)

envoy_cc_library(
    name = "filter_manager_lib",
    srcs = ["filter_manager_impl.cc"],
    hdrs = ["filter_manager_impl.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_library(
    name = "hash_policy_lib",
    srcs = ["hash_policy.cc"],
    hdrs = ["hash_policy.h"],
    deps = [
        "//envoy/common:hashable_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:hash_policy_interface",
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
        "//envoy/api:io_error_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "lc_trie_lib",
    hdrs = ["lc_trie.h"],
    deps = [
        ":address_lib",
        ":cidr_range_lib",
        ":utility_lib",
        "//source/common/common:assert_lib",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/numeric:int128",
    ],
)

envoy_cc_library(
    name = "socket_interface_lib",
    hdrs = ["socket_interface.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/network:socket_interface_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "default_socket_interface_lib",
    srcs = [
        "io_socket_handle_base_impl.cc",
        "io_socket_handle_impl.cc",
        "socket_interface_impl.cc",
        "win32_socket_handle_impl.cc",
    ] + select({
        "//bazel:android": [],
        "//bazel:linux": [
            "io_uring_socket_handle_impl.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = [
        "io_socket_handle_base_impl.h",
        "io_socket_handle_impl.h",
        "socket_interface_impl.h",
        "win32_socket_handle_impl.h",
    ] + select({
        "//bazel:android": [],
        "//bazel:linux": [
            "io_uring_socket_handle_impl.h",
        ],
        "//conditions:default": [],
    }),
    deps = [
        ":address_lib",
        ":io_socket_error_lib",
        ":socket_interface_lib",
        ":socket_lib",
        "//envoy/common/io:io_uring_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:io_handle_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "@com_github_google_quiche//:quic_core_lru_cache_lib",
        "@com_github_google_quiche//:quic_platform_socket_address",
        "@envoy_api//envoy/extensions/network/socket_interface/v3:pkg_cc_proto",
    ] + select({
        "//bazel:linux": [
            "//source/common/io:io_uring_impl_lib",
            "//source/common/io:io_uring_worker_factory_impl_lib",
            "//source/common/io:io_uring_worker_lib",
        ],
        "//conditions:default": [],
    }),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "socket_lib",
    srcs = ["socket_impl.cc"],
    hdrs = ["socket_impl.h"],
    deps = [
        "//envoy/network:listener_interface",
        "//envoy/network:socket_interface",
        "//envoy/network:socket_interface_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "connection_socket_lib",
    hdrs = ["connection_socket_impl.h"],
    deps = [
        ":socket_lib",
        ":utility_lib",
        "//envoy/network:exception_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "listen_socket_lib",
    srcs = ["listen_socket_impl.cc"],
    hdrs = ["listen_socket_impl.h"],
    deps = [
        ":connection_socket_lib",
        ":socket_lib",
        ":utility_lib",
        "//envoy/network:exception_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_filter_buffer_lib",
    srcs = ["listener_filter_buffer_impl.cc"],
    hdrs = ["listener_filter_buffer_impl.h"],
    deps = [
        "//envoy/network:io_handle_interface",
        "//envoy/network:listener_filter_buffer_interface",
        "//source/common/buffer:buffer_lib",
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
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/network:exception_interface",
        "//envoy/network:listener_interface",
        "//envoy/network:parent_drained_callback_registrar_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/event:dispatcher_includes",
        "//source/common/runtime:runtime_keys_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "raw_buffer_socket_lib",
    srcs = ["raw_buffer_socket.cc"],
    hdrs = ["raw_buffer_socket.h"],
    deps = [
        ":utility_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:headers_lib",
        "//source/common/network:transport_socket_options_lib",
    ],
)

envoy_cc_library(
    name = "resolver_lib",
    srcs = ["resolver_impl.cc"],
    hdrs = ["resolver_impl.h"],
    deps = [
        ":utility_lib",
        "//envoy/network:address_interface",
        "//envoy/network:resolver_interface",
        "//envoy/registry",
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
    deps = [
        ":address_lib",
        "//envoy/api:os_sys_calls_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "addr_family_aware_socket_option_lib",
    srcs = ["addr_family_aware_socket_option_impl.cc"],
    hdrs = ["addr_family_aware_socket_option_impl.h"],
    deps = [
        ":address_lib",
        ":socket_lib",
        ":socket_option_lib",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "win32_redirect_records_option_lib",
    srcs = ["win32_redirect_records_option_impl.cc"],
    hdrs = ["win32_redirect_records_option_impl.h"],
    deps = [
        ":address_lib",
        ":socket_lib",
        ":socket_option_lib",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_option_factory_lib",
    srcs = ["socket_option_factory.cc"],
    hdrs = ["socket_option_factory.h"],
    deps = [
        ":addr_family_aware_socket_option_lib",
        ":address_lib",
        ":socket_option_lib",
        ":win32_redirect_records_option_lib",
        "//envoy/network:listen_socket_interface",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/types:optional",
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
        ":socket_option_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:listener_interface",
        "//envoy/stats:stats_interface",
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
        ":filter_state_proxy_info_lib",
        ":proxy_protocol_filter_state_lib",
        ":upstream_server_name_lib",
        ":upstream_subject_alt_names_lib",
        "//envoy/common:hashable_interface",
        "//envoy/common:optref_lib",
        "//envoy/network:proxy_protocol_options_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "filter_state_proxy_info_lib",
    srcs = ["filter_state_proxy_info.cc"],
    hdrs = ["filter_state_proxy_info.h"],
    deps = [
        "//envoy/network:address_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "filter_state_dst_address_lib",
    srcs = ["filter_state_dst_address.cc"],
    hdrs = ["filter_state_dst_address.h"],
    deps = [
        ":utility_lib",
        "//envoy/common:hashable_interface",
        "//envoy/network:address_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:hash_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "upstream_server_name_lib",
    srcs = ["upstream_server_name.cc"],
    hdrs = ["upstream_server_name.h"],
    deps = [
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "upstream_subject_alt_names_lib",
    srcs = ["upstream_subject_alt_names.cc"],
    hdrs = ["upstream_subject_alt_names.h"],
    deps = [
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "filter_matcher_lib",
    srcs = [
        "filter_matcher.cc",
    ],
    hdrs = ["filter_matcher.h"],
    deps = [
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "@com_google_absl//absl/strings:str_format",
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
        "//envoy/network:socket_interface",
        "//envoy/network:udp_packet_writer_handler_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "proxy_protocol_filter_state_lib",
    srcs = ["proxy_protocol_filter_state.cc"],
    hdrs = ["proxy_protocol_filter_state.h"],
    deps = [
        "//envoy/network:proxy_protocol_options_lib",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "upstream_socket_options_filter_state_lib",
    srcs = ["upstream_socket_options_filter_state.cc"],
    hdrs = ["upstream_socket_options_filter_state.h"],
    deps = [
        ":socket_interface_lib",
        ":socket_option_factory_lib",
        "//envoy/network:io_handle_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "generic_listener_filter_impl_base_lib",
    hdrs = ["generic_listener_filter_impl_base.h"],
    deps = [
        "//envoy/network:filter_interface",
    ],
)
