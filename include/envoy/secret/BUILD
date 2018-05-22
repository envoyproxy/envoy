licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_interface",
    hdrs = ["secret.h"],
)

envoy_cc_library(
    name = "secret_manager_interface",
    hdrs = ["secret_manager.h"],
    deps = [
        ":secret_interface",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)
