package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_cc_test_library")

envoy_cc_test(
    name = "example_configs_test",
    srcs = [
        "example_configs_test.cc",
        "example_configs_test.h",
    ],
    data = [
        "example_configs_test_setup.sh",
        "//configs:example_configs",
    ],
    deps = [
        "//source/server:configuration_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
    ],
)

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    external_deps = [
        "googletest",
        "protobuf",
    ],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/common:version_lib",
        "//source/common/event:libevent_lib",
        "//source/common/ssl:openssl_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:printers_lib",
    ],
)
