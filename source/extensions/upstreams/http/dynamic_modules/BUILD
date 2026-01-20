load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "bridge_config_lib",
    srcs = ["bridge_config.cc"],
    hdrs = ["bridge_config.h"],
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "http_tcp_bridge_lib",
    srcs = ["http_tcp_bridge.cc"],
    hdrs = ["http_tcp_bridge.h"],
    deps = [
        ":bridge_config_lib",
        "//envoy/router:router_interface",
        "//envoy/tcp:conn_pool_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    deps = [
        ":bridge_config_lib",
        ":http_tcp_bridge_lib",
        "//envoy/router:router_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":http_tcp_bridge_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":bridge_config_lib",
        ":conn_pool_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/upstreams/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)
