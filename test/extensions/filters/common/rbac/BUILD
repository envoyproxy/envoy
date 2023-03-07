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

envoy_extension_cc_test(
    name = "matchers_test",
    srcs = ["matchers_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/common/rbac:matchers_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "engine_impl_test",
    srcs = ["engine_impl_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/rbac:engine_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_mock(
    name = "engine_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/rbac:engine_lib",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
    ],
)
