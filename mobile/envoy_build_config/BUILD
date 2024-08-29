load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_mobile_package",
    "envoy_select_envoy_mobile_listener",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "extension_registry",
    srcs = [
        "extension_registry.cc",
    ],
    hdrs = ["extension_registry.h"],
    repository = "@envoy",
    deps = [
        "extension_registry_platform_additions",
        "@envoy//source/common/http/matching:inputs_lib",
        "@envoy//source/common/network:socket_lib",
        "@envoy//source/common/quic:quic_transport_socket_factory_lib",
        "@envoy//source/common/router:upstream_codec_filter_lib",
        "@envoy//source/common/tls/cert_validator:cert_validator_lib",
        "@envoy//source/common/upstream:default_local_address_selector_factory",
        "@envoy//source/common/watchdog:abort_action_config",
        "@envoy//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "@envoy//source/extensions/compression/brotli/decompressor:config",
        "@envoy//source/extensions/compression/gzip/decompressor:config",
        "@envoy//source/extensions/filters/http/alternate_protocols_cache:config",
        "@envoy//source/extensions/filters/http/buffer:config",
        "@envoy//source/extensions/filters/http/decompressor:config",
        "@envoy//source/extensions/filters/http/dynamic_forward_proxy:config",
        "@envoy//source/extensions/filters/http/router:config",
        "@envoy//source/extensions/filters/network/http_connection_manager:config",
        "@envoy//source/extensions/http/header_formatters/preserve_case:config",
        "@envoy//source/extensions/load_balancing_policies/cluster_provided:config",
        "@envoy//source/extensions/network/dns_resolver/getaddrinfo:config",
        "@envoy//source/extensions/path/match/uri_template:config",
        "@envoy//source/extensions/path/rewrite/uri_template:config",
        "@envoy//source/extensions/request_id/uuid:config",
        "@envoy//source/extensions/transport_sockets/http_11_proxy:upstream_config",
        "@envoy//source/extensions/transport_sockets/raw_buffer:config",
        "@envoy//source/extensions/transport_sockets/tls:upstream_config",
        "@envoy//source/extensions/upstreams/http/generic:config",
        "@envoy_mobile//library/common/extensions/cert_validator/platform_bridge:config",
        "@envoy_mobile//library/common/extensions/filters/http/local_error:config",
        "@envoy_mobile//library/common/extensions/filters/http/network_configuration:config",
        "@envoy_mobile//library/common/extensions/filters/http/platform_bridge:config",
        "@envoy_mobile//library/common/extensions/filters/http/socket_tag:config",
        "@envoy_mobile//library/common/extensions/key_value/platform:config",
        "@envoy_mobile//library/common/extensions/listener_managers/api_listener_manager:api_listener_manager_lib",
        "@envoy_mobile//library/common/extensions/retry/options/network_configuration:config",
    ] + envoy_select_envoy_mobile_listener(
        [
            "@envoy//source/extensions/udp_packet_writer/default:config",
            "@envoy//source/common/listener_manager:listener_manager_lib",
            "@envoy//source/common/listener_manager:connection_handler_lib",
        ],
        "@envoy",
    ) + select({
        "@envoy//bazel:apple": [],
        "//conditions:default": ["@envoy//source/extensions/network/dns_resolver/cares:config"],
    }),
)

envoy_cc_library(
    name = "extension_registry_platform_additions",
    srcs = select({
        "@envoy//bazel:apple": ["extension_registry_apple.cc"],
        "//conditions:default": ["extension_registry_noop.cc"],
    }),
    hdrs = ["extension_registry_platform_additions.h"],
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": ["@envoy//source/extensions/network/dns_resolver/apple:config"],
        "//conditions:default": [],
    }),
)

# for java and C++ auto-register the test libraries.
envoy_cc_library(
    name = "test_extensions",
    srcs = [
        "test_extensions_autoregister.cc",
    ],
    repository = "@envoy",
    deps = [":test_extensions_no_autoregister"],
    alwayslink = 1,
)

# swift may compile out autoregistry functions, so registration will be performed manually.
envoy_cc_library(
    name = "test_extensions_no_autoregister",
    srcs = [
        "test_extensions.cc",
    ],
    hdrs = [
        "test_extensions.h",
    ],
    repository = "@envoy",
    # List of modules to compile into the test files. For JVM we always compile these into the .so all
    # the time, while for iOS we have to force register the extensions, so test files must pull in
    # the TestExtensions module and call register_test_extensions().
    deps = [
        "@envoy//source/extensions/clusters/static:static_cluster_lib",
        "@envoy//source/extensions/filters/http/buffer:config",
        "@envoy//source/extensions/load_balancing_policies/round_robin:config",
        "@envoy_mobile//test/common/http/filters/assertion:config",
        "@envoy_mobile//test/common/http/filters/assertion:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/route_cache_reset:config",
        "@envoy_mobile//test/common/http/filters/route_cache_reset:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_accessor:config",
        "@envoy_mobile//test/common/http/filters/test_accessor:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_event_tracker:config",
        "@envoy_mobile//test/common/http/filters/test_event_tracker:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_kv_store:config",
        "@envoy_mobile//test/common/http/filters/test_kv_store:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_logger:config",
        "@envoy_mobile//test/common/http/filters/test_logger:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_read:config",
        "@envoy_mobile//test/common/http/filters/test_read:filter_cc_proto_descriptor",
        "@envoy_mobile//test/common/http/filters/test_remote_response:config",
        "@envoy_mobile//test/common/http/filters/test_remote_response:filter_cc_proto_descriptor",
    ],
    alwayslink = 1,
)
