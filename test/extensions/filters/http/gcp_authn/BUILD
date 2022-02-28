load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(tyxia) Add test
envoy_extension_cc_test(
    name = "filter_config_test",
    size = "small",
    srcs = ["filter_config_test.cc"],
    extension_names = ["envoy.filters.http.gcp_authn"],
    deps = [
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/gcp_authn:filter_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "gcp_authn_filter_test",
    size = "small",
    srcs = ["gcp_authn_filter_test.cc"],
    extension_names = ["envoy.filters.http.gcp_authn"],
    deps = [
        # ":gcp_authn_mocks",
        "//source/extensions/filters/http/gcp_authn:gcp_authn_lib",
        "//source/extensions/filters/http/gcp_authn:filter_config",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "gcp_authn_filter_integration_test",
    srcs = ["gcp_authn_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.gcp_authn"],
    deps = [
        "//source/extensions/filters/http/gcp_authn:filter_config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_extension_cc_mock(
    name = "gcp_authn_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.gcp_authn"],
    deps = [
        "//source/extensions/filters/http/gcp_authn:gcp_authn_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)
