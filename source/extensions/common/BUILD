load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    # Legacy.  TODO(#9953) clean up.
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)
