load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "http_subscription_impl_test",
    srcs = ["http_subscription_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_subscription_test_harness",
    ],
)

envoy_cc_test_library(
    name = "http_subscription_test_harness",
    hdrs = ["http_subscription_test_harness.h"],
    deps = [
        "//envoy/http:async_client_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/config_subscription/rest:http_subscription_lib",
        "//test/common/config:subscription_test_harness",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
