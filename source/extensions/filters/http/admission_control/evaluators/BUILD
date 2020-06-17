load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that probabilistically rejects requests based on upstream success-rate.

envoy_package()

envoy_cc_library(
    name = "response_evaluator_lib",
    srcs = ["success_criteria_evaluator.cc"],
    hdrs = [
        "response_evaluator.h",
        "success_criteria_evaluator.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/grpc:status",
        "//source/common/common:enum_to_int",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)
