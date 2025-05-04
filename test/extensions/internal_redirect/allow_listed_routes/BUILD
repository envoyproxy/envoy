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
    extension_names = ["envoy.internal_redirect_predicates.allow_listed_routes"],
    deps = [
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/internal_redirect/allow_listed_routes:config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/internal_redirect/allow_listed_routes/v3:pkg_cc_proto",
    ],
)
