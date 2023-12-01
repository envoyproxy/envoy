load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_mobile_package",
    "envoy_select_envoy_mobile_xds",
    "envoy_select_signal_trace",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "client_integration_test",
    srcs = ["client_integration_test.cc"],
    exec_properties = {
        # TODO(willengflow): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    repository = "@envoy",
    deps = [
        ":base_client_integration_test_lib",
        "//test/common/mocks/common:common_mocks",
        "@envoy//source/common/quic:active_quic_listener_lib",
        "@envoy//source/common/quic:client_connection_factory_lib",
        "@envoy//source/common/quic:quic_server_factory_lib",
        "@envoy//source/common/quic:quic_transport_socket_factory_lib",
        "@envoy//source/common/quic:udp_gso_batch_writer_lib",
        "@envoy//source/extensions/udp_packet_writer/gso:config",
    ],
)

envoy_cc_test(
    name = "rtds_integration_test",
    srcs = envoy_select_envoy_mobile_xds(
        ["rtds_integration_test.cc"],
        "@envoy",
    ),
    data = [
        "@envoy//test/config/integration/certs",
    ],
    exec_properties = {
        # TODO(willengflow): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    external_deps = [
        "abseil_strings",
    ],
    repository = "@envoy",
    deps = [
        ":xds_integration_test_lib",
        "@envoy//test/test_common:environment_lib",
        "@envoy//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cds_integration_test",
    srcs = envoy_select_envoy_mobile_xds(
        ["cds_integration_test.cc"],
        "@envoy",
    ),
    data = [
        "@envoy//test/config/integration/certs",
    ],
    exec_properties = {
        # TODO(willengflow): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    external_deps = [
        "abseil_strings",
    ],
    repository = "@envoy",
    deps = [
        ":xds_integration_test_lib",
        "@envoy//test/test_common:environment_lib",
        "@envoy//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_integration_test",
    srcs = envoy_select_envoy_mobile_xds(
        ["sds_integration_test.cc"],
        "@envoy",
    ),
    data = [
        "@envoy//test/config/integration/certs",
    ],
    exec_properties = {
        # TODO(willengflow): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    repository = "@envoy",
    deps = [
        ":xds_integration_test_lib",
        "@envoy//source/common/config:protobuf_link_hacks",
        "@envoy//source/extensions/transport_sockets/tls:config",
        "@envoy//source/extensions/transport_sockets/tls:context_config_lib",
        "@envoy//source/extensions/transport_sockets/tls:context_lib",
        "@envoy//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
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
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "xds_integration_test_lib",
    srcs = [
        "xds_integration_test.cc",
    ],
    hdrs = [
        "xds_integration_test.h",
    ],
    repository = "@envoy",
    deps = [
        ":base_client_integration_test_lib",
        "@envoy//source/common/config:api_version_lib",
        "@envoy//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

# interface libs for test servers` jni implementations
envoy_cc_test_library(
    name = "test_server_interface_lib",
    srcs = [
        "test_server.cc",
        "test_server_interface.cc",
    ],
    hdrs = [
        "test_server.h",
        "test_server_interface.h",
    ],
    data = [
        "@envoy//test/config/integration/certs",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//source/exe:process_wide_lib",
        "@envoy//test/integration:autonomous_upstream_lib",
        "@envoy//test/integration:utility_lib",
        "@envoy//test/mocks/server:transport_socket_factory_context_mocks",
        "@envoy//test/test_common:environment_lib",
        "@envoy_build_config//:extension_registry",
    ] + envoy_select_signal_trace(
        ["@envoy//source/common/signal:sigaction_lib"],
        "@envoy",
    ),
)

envoy_cc_test_library(
    name = "xds_test_server_interface_lib",
    srcs = [
        "xds_test_server.cc",
        "xds_test_server_interface.cc",
    ],
    hdrs = [
        "xds_test_server.h",
        "xds_test_server_interface.h",
    ],
    repository = "@envoy",
    deps = [
        ":base_client_integration_test_lib",
        "@envoy//source/common/event:libevent_lib",
        "@envoy//source/exe:process_wide_lib",
        "@envoy//source/extensions/transport_sockets/tls:context_config_lib",
        "@envoy//source/extensions/transport_sockets/tls:context_lib",
        "@envoy//source/extensions/transport_sockets/tls:ssl_socket_lib",
        "@envoy//test/integration:autonomous_upstream_lib",
        "@envoy//test/integration:utility_lib",
        "@envoy//test/mocks/server:transport_socket_factory_context_mocks",
        "@envoy//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_build_config//:extension_registry",
    ] + envoy_select_signal_trace(
        ["@envoy//source/common/signal:sigaction_lib"],
        "@envoy",
    ),
)
