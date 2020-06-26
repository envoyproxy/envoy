load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that probabilistically rejects requests based on upstream success-rate.
# Public docs: docs/root/configuration/http_filters/admission_control.rst

envoy_package()

envoy_cc_extension(
    name = "admission_control_filter_lib",
    srcs = [
        "admission_control.cc",
        "thread_local_controller.cc",
    ],
    hdrs = [
        "admission_control.h",
        "thread_local_controller.h",
    ],
    security_posture = "unknown",
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/http:codes_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/admission_control/evaluators:response_evaluator_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//include/envoy/registry",
        "//source/common/common:enum_to_int",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/admission_control:admission_control_filter_lib",
        "//source/extensions/filters/http/admission_control/evaluators:response_evaluator_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)
