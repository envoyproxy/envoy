load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

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
    ],
)

envoy_cc_test(
    name = "rtds_integration_test",
    srcs = ["rtds_integration_test.cc"],
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
        "@envoy//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_integration_test",
    srcs = [
        "sds_integration_test.cc",
    ],
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
        "@envoy//test/common/grpc:grpc_client_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

# interface libs for quic test server's jni implementation
envoy_cc_test_library(
    name = "quic_test_server_interface_lib",
    srcs = [
        "quic_test_server.cc",
        "quic_test_server_interface.cc",
    ],
    hdrs = [
        "quic_test_server.h",
        "quic_test_server_interface.h",
    ],
    data = [
        "@envoy//test/config/integration/certs",
    ],
    repository = "@envoy",
    deps = [
        ":autonomous_upstream_lib",
        "@envoy//source/exe:process_wide_lib",
        "@envoy//test/config:utility_lib",
        "@envoy//test/mocks/server:transport_socket_factory_context_mocks",
        "@envoy//test/test_common:environment_lib",
    ] + select({
        "@envoy//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "@envoy//source/common/signal:sigaction_lib",
        ],
    }),
)

envoy_cc_test_library(
    name = "autonomous_upstream_lib",
    srcs = [
        "autonomous_upstream.cc",
    ],
    hdrs = [
        "autonomous_upstream.h",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//test/integration:fake_upstream_lib",
    ],
)
