licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "ssl_integration_test",
    srcs = [
        "ssl_integration_test.cc",
        "ssl_integration_test.h",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
