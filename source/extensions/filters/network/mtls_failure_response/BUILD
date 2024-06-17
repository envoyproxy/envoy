load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mtls_failure_response",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/common:shared_token_bucket_impl_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mtls_failure_response/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "mtls_failure_response",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:shared_token_bucket_impl_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mtls_failure_response/v3:pkg_cc_proto",
    ],
)
