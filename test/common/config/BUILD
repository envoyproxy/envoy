licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "filesystem_subscription_impl_test",
    srcs = ["filesystem_subscription_impl_test.cc"],
    external_deps = ["envoy_eds"],
    deps = [
        "//source/common/config:filesystem_subscription_lib",
        "//source/common/event:dispatcher_lib",
        "//test/mocks/config:config_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "grpc_subscription_impl_test",
    srcs = ["grpc_subscription_impl_test.cc"],
    external_deps = ["envoy_eds"],
    deps = [
        "//source/common/config:grpc_subscription_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http_subscription_impl_test",
    srcs = ["http_subscription_impl_test.cc"],
    external_deps = [
        "envoy_eds",
        "protobuf",
    ],
    deps = [
        "//include/envoy/http:async_client_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:http_subscription_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)
