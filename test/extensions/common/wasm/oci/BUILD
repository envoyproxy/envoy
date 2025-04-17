load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "oci_async_datasource_test",
    srcs = ["oci_async_datasource_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/common:empty_string",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/wasm/oci:oci_async_datasource_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
