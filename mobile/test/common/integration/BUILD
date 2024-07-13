load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_mobile_package",
    "envoy_select_signal_trace",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "client_integration_test",
    srcs = ["client_integration_test.cc"],
    linkopts = select({
        "@envoy//bazel:apple": [
            # For the TestProxyResolutionApi test.
            "-Wl,-framework,CoreFoundation",
            "-Wl,-framework,CFNetwork",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    shard_count = 6,
    deps = [
        ":base_client_integration_test_lib",
        "//test/common/mocks/common:common_mocks",
        "@envoy//source/common/quic:active_quic_listener_lib",
        "@envoy//source/common/quic:client_connection_factory_lib",
        "@envoy//source/common/quic:quic_server_factory_lib",
        "@envoy//source/common/quic:quic_server_transport_socket_factory_lib",
        "@envoy//source/common/quic:quic_transport_socket_factory_lib",
        "@envoy//source/common/quic:udp_gso_batch_writer_lib",
        "@envoy//source/extensions/udp_packet_writer/gso:config",
        "@envoy//test/extensions/filters/http/dynamic_forward_proxy:test_resolver_lib",
        "@envoy//test/test_common:test_random_generator_lib",
    ],
)

envoy_cc_test_library(
    name = "base_client_integration_test_lib",
    srcs = [
        "base_client_integration_test.cc",
    ],
    hdrs = [
        "base_client_integration_test.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//test/common/http:common_lib",
        "@envoy//test/integration:http_integration_lib_light",
        "@envoy//test/test_common:utility_lib",
    ],
)

# interface libs for test servers` jni implementations
envoy_cc_test_library(
    name = "test_server_lib",
    srcs = [
        "test_server.cc",
    ],
    hdrs = [
        "test_server.h",
    ],
    data = [
        "@envoy//test/config/integration/certs",
    ],
    repository = "@envoy",
    deps = [
        "//library/common:engine_common_lib",
        "@envoy//source/common/listener_manager:listener_manager_lib",
        "@envoy//source/exe:process_wide_lib",
        "@envoy//test/integration:autonomous_upstream_lib",
        "@envoy//test/integration:utility_lib",
        "@envoy//test/mocks/server:server_factory_context_mocks",
        "@envoy//test/mocks/server:transport_socket_factory_context_mocks",
        "@envoy//test/test_common:environment_lib",
        "@envoy_build_config//:extension_registry",
    ] + envoy_select_signal_trace(
        ["@envoy//source/common/signal:sigaction_lib"],
        "@envoy",
    ),
)

envoy_cc_test_library(
    name = "test_server_interface_lib",
    srcs = [
        "test_server_interface.cc",
    ],
    hdrs = [
        "test_server_interface.h",
    ],
    repository = "@envoy",
    deps = [
        ":test_server_lib",
    ],
)

envoy_cc_test_library(
    name = "engine_with_test_server",
    srcs = [
        "engine_with_test_server.cc",
    ],
    hdrs = [
        "engine_with_test_server.h",
    ],
    repository = "@envoy",
    deps = [
        ":test_server_lib",
        "//library/cc:engine_builder_lib",
    ],
)
