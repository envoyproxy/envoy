load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "ocsp_lib",
    srcs = ["ocsp.cc"],
    hdrs = ["ocsp.h"],
    repository = "",
    deps = [
        ":asn1_utility_lib",
        "//envoy/common:time_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/tls:utility_lib",
    ],
)

envoy_cc_library(
    name = "asn1_utility_lib",
    srcs = ["asn1_utility.cc"],
    hdrs = ["asn1_utility.h"],
    repository = "",
    deps = [
        "//envoy/common:time_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:c_smart_ptr_lib",
    ],
)
