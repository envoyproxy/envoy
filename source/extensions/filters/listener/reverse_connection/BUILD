load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "reverse_connection_lib",
    srcs = ["reverse_connection.cc"],
    hdrs = ["reverse_connection.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config_lib",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_connection_utility_lib",
    ],
)

envoy_cc_extension(
    name = "config_factory_lib",
    srcs = ["config_factory.cc"],
    hdrs = ["config_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":config_lib",
        ":reverse_connection_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
)
