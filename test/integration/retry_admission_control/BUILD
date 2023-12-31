load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "custom_retry_admission_controller",
    srcs = [
        "custom_retry_admission_controller.cc",
    ],
    hdrs = [
        "custom_retry_admission_controller.h",
    ],
    deps = [
        ":config_cc_proto",
        "//envoy/upstream:admission_control_interface",
        "//test/test_common:registry_lib",
    ],
)

envoy_proto_library(
    name = "config",
    srcs = [":config.proto"],
)
