load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "metadata_object_test",
    srcs = ["metadata_object_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//envoy/registry",
    ],
)

envoy_cc_test(
    name = "hashable_string_test",
    srcs = ["hashable_string_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:hashable_string_lib",
        "//envoy/common:hashable_interface",
        "//source/extensions/filters/common/set_filter_state:filter_config_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)
