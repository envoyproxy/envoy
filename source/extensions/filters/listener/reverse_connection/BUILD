load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "reverse_connection_lib",
    srcs = ["reverse_connection.cc"],
    hdrs = ["reverse_connection.h"],
    deps = [
        ":config_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        # "//source/common/network:io_socket_handle_lib",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",  # The extension build system requires a library named config
    srcs = ["config_factory.cc"],
    hdrs = ["config_factory.h"],
    # category = "envoy.filters.listener",
    # security_posture = "robust_to_untrusted_downstream",
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    deps = [
        ":config_lib",
        ":reverse_connection_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        # "//source/extensions/filters/listener:well_known_names",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
)
