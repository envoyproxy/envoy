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
    name = "http_capsule_filter_test",
    srcs = ["http_capsule_filter_test.cc"],
    extension_names = ["envoy.filters.udp.session.http_capsule"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/udp/udp_proxy/session_filters/http_capsule:config",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_capsule_integration_test",
    srcs = ["http_capsule_integration_test.cc"],
    extension_names = ["envoy.filters.udp.session.http_capsule"],
    rbe_pool = "2core",
    deps = [
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/http_capsule:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/http_capsule:http_capsule_filter_lib",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
