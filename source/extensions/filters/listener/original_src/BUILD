licenses(["notice"])  # Apache 2

# A filter for mirroring the downstream remote address on the upstream's source.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "@envoy_api//envoy/config/filter/listener/original_src/v2alpha1:original_src_cc",
    ],
)

envoy_cc_library(
    name = "original_src_socket_option_lib",
    srcs = ["original_src_socket_option.cc"],
    hdrs = ["original_src_socket_option.h"],
    deps = [
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "original_src_lib",
    srcs = ["original_src.cc"],
    hdrs = ["original_src.h"],
    deps = [
        ":config_lib",
        ":original_src_socket_option_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:socket_option_factory_lib",
    ],
)

envoy_cc_library(
    name = "config",  # The extension build system requires a library named config
    srcs = ["original_src_config_factory.cc"],
    hdrs = ["original_src_config_factory.h"],
    deps = [
        ":config_lib",
        ":original_src_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
        "@envoy_api//envoy/config/filter/listener/original_src/v2alpha1:original_src_cc",
    ],
)
