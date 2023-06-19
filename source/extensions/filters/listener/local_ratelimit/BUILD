load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = ["local_ratelimit.cc"],
    hdrs = ["local_ratelimit.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "@envoy_api//envoy/extensions/filters/listener/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/listener/local_ratelimit:local_ratelimit_lib",
        "@envoy_api//envoy/extensions/filters/listener/local_ratelimit/v3:pkg_cc_proto",
    ],
)
