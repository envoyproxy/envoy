licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_impl_lib",
    srcs = ["secret_impl.cc"],
    hdrs = ["secret_impl.h"],
    deps = [
        "//include/envoy/secret:secret_interface",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "secret_manager_impl_lib",
    srcs = ["secret_manager_impl.cc"],
    hdrs = ["secret_manager_impl.h"],
    deps = [
        ":secret_impl_lib",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:instance_interface",
    ],
)
