load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "api_listener_lib",
    srcs = [
        "api_listener_impl.cc",
    ],
    hdrs = [
        "api_listener_impl.h",
    ],
    extra_visibility = [
        "//test/common:__subpackages__",
        "//test/integration:__subpackages__",
    ],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:api_listener_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:empty_string",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/listener_manager:listener_info_lib",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/udp_packet_writer/default:config",
        "//source/extensions/upstreams/http/generic:config",
        "//source/server:configuration_lib",
        "//source/server:factory_context_lib",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
    alwayslink = True,
)
