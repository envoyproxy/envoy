load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "concurrency_budget_lib",
    srcs = ["concurrency_budget.cc"],
    hdrs = ["concurrency_budget.h"],
    deps = [
        "//envoy/upstream:admission_control_interface",
        "//source/common/runtime:runtime_features_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        # previously considered core code.
        "//test:__subpackages__",
    ],
    deps = [
        ":concurrency_budget_lib",
        "//envoy/registry",
        "//envoy/upstream:admission_control_interface",
        "@envoy_api//envoy/extensions/retry/admission_control/concurrency_budget/v3:pkg_cc_proto",
    ],
)
