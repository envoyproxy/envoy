licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "admin_test",
    srcs = ["admin_test.cc"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/profiler:profiler_lib",
        "//source/server/http:admin_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_tracker_impl_test",
    srcs = ["config_tracker_impl_test.cc"],
    deps = [
        "//source/server/http:config_tracker_lib",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "health_check_test",
    srcs = ["health_check_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/server/http:health_check_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
