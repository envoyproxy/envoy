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
    name = "filter_state_test",
    srcs = ["filter_state_test.cc"],
    extension_names = ["envoy.internal_redirect_predicates.filter_state"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stream_info:bool_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/internal_redirect/filter_state:config",
        "@envoy_api//envoy/extensions/internal_redirect/filter_state/v3:pkg_cc_proto",
    ],
)
