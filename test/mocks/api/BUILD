licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
    "envoy_select_hot_restart",
)

envoy_package()

envoy_cc_mock(
    name = "api_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"] + envoy_select_hot_restart(["hot_restart.h"]),
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/api:os_sys_calls_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_time_lib",
    ],
)
