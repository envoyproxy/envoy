load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "local_info_lib",
    hdrs = ["local_info_impl.h"],
    deps = [
        "//envoy/local_info:local_info_interface",
        "//source/common/config:context_provider_lib",
        "//source/common/stats:symbol_table_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
