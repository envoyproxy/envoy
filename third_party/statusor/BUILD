licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "statusor_lib",
    srcs = [
        "statusor.cc",
    ],
    hdrs = [
        "statusor.h",
        "statusor_internals.h",
    ],
    external_deps = [
        "abseil_status",
    ],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_test(
    name = "statusor_test",
    srcs = ["statusor_test.cc"],
    deps = [
        ":statusor_lib",
    ],
)
