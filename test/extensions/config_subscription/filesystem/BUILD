load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filesystem_subscription_impl_test",
    srcs = ["filesystem_subscription_impl_test.cc"],
    deps = [
        ":filesystem_subscription_test_harness",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "filesystem_subscription_test_harness",
    hdrs = ["filesystem_subscription_test_harness.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
        "//test/common/config:subscription_test_harness",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
