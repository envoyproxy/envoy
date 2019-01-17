licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "ssl_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//include/envoy/tls:connection_interface",
        "//include/envoy/tls:context_config_interface",
        "//include/envoy/tls:context_interface",
        "//include/envoy/tls:context_manager_interface",
        "//test/mocks/secret:secret_mocks",
    ],
)
