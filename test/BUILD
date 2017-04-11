package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

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
