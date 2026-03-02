load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "host_specific_config",
    srcs = ["host_specific_config.cc"],
    hdrs = ["host_specific_config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/upstreams/host_specific_http/v3:pkg_cc_proto",
    ],
)
