load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_cc_test_library", "envoy_package")

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
        "//library/cc:engine_builder_lib",
        "//library/common/extensions/filters/http/local_error:config",
        "//library/common/extensions/filters/http/local_error:filter_cc_proto",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/extensions/http/header_formatters/preserve_case:config",
        "@envoy//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "@envoy//test/common/http:common_lib",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/server:utility_lib",
        "@envoy//test/test_common:environment_lib",
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
