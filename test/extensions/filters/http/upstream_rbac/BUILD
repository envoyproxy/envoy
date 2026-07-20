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
    extension_names = ["envoy.filters.http.upstream_rbac"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/upstream_rbac:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "upstream_rbac_filter_test",
    srcs = ["upstream_rbac_filter_test.cc"],
    extension_names = ["envoy.filters.http.upstream_rbac"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/common/stream_info:upstream_address_lib",
        "//source/extensions/filters/http/upstream_rbac:upstream_rbac_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/matchers/upstream_ip_port/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "upstream_rbac_integration_test",
    size = "large",
    srcs = ["upstream_rbac_integration_test.cc"],
    extension_names = ["envoy.filters.http.upstream_rbac"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/router:upstream_codec_filter_lib",
        "//source/extensions/filters/http/upstream_rbac:config",
        "//test/config:utility_lib",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/matchers/upstream_ip_port/v3:pkg_cc_proto",
    ],
)
