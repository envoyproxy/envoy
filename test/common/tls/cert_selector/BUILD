load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "async_cert_selector",
    srcs = [
        "async_cert_selector.cc",
        "stats.cc",
    ],
    hdrs = [
        "async_cert_selector.h",
        "stats.h",
    ],
    deps = [
        "//source/common/tls:server_context_lib",
    ],
)
