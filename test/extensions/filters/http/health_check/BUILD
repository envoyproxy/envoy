licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "health_check_test",
    srcs = ["health_check_test.cc"],
    extension_name = "envoy.filters.http.health_check",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/health_check:health_check_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.health_check",
    deps = [
        "//source/extensions/filters/http/health_check:config",
        "//test/mocks/server:server_mocks",
    ],
)
