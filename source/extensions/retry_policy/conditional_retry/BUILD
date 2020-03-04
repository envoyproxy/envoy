licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "conditional_retry_lib",
    srcs = ["conditional_retry.cc"],
    hdrs = ["conditional_retry.h"],
    deps = [
        "//include/envoy/router:router_interface",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":conditional_retry_lib",
        "//include/envoy/registry",
        "//source/common/protobuf",
        "//source/common/router:retry_state_lib",
        "//source/extensions/retry_policy:well_known_names",
        "@envoy_api//envoy/extensions/retry_policy/conditional_retry/v3:pkg_cc_proto",
    ],
)
