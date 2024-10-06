load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "ssl_integration_test_lib",
    srcs = [
        "ssl_integration_test_base.cc",
    ],
    hdrs = ["ssl_integration_test_base.h"],
    rbe_pool = "2core",
    deps = [
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/integration:http_integration_lib",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ssl_integration_test",
    size = "large",
    srcs = [
        "ssl_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":sni_to_header_filter_lib",
        ":ssl_integration_test_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:ssl_handshaker_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/common/tls/cert_selector:async_cert_selector",
        "//test/common/tls/cert_validator:timed_cert_validator",
        "//test/integration/filters:stream_info_to_headers_filter_lib",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "sni_to_header_filter_lib",
    srcs = [
        "sni_to_header_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/integration/filters:common_lib",
    ],
)
