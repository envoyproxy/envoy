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
    name = "redirect_extension_integration_test",
    size = "large",
    srcs = [
        "redirect_extension_integration_test.cc",
    ],
    extension_names = [
        "envoy.internal_redirect_predicates.allow_listed_routes",
        "envoy.internal_redirect_predicates.filter_state",
        "envoy.internal_redirect_predicates.previous_routes",
        "envoy.internal_redirect_predicates.safe_cross_scheme",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/set_filter_state:config",
        "//source/extensions/internal_redirect/allow_listed_routes:config",
        "//source/extensions/internal_redirect/filter_state:config",
        "//source/extensions/internal_redirect/previous_routes:config",
        "//source/extensions/internal_redirect/safe_cross_scheme:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/internal_redirect/allow_listed_routes/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/internal_redirect/filter_state/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/internal_redirect/previous_routes/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/internal_redirect/safe_cross_scheme/v3:pkg_cc_proto",
    ],
)
