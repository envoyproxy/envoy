load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.router"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/router:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "auto_sni_integration_test",
    size = "large",
    srcs = ["auto_sni_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.filters.http.router"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/router:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
