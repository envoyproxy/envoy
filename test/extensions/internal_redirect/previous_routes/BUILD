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
    extension_names = ["envoy.internal_redirect_predicates.previous_routes"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/internal_redirect/previous_routes:config",
        "@envoy_api//envoy/extensions/internal_redirect/previous_routes/v3:pkg_cc_proto",
    ],
)
