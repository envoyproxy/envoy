licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config_interface",
    hdrs = ["config.h"],
    deps = [
        ":listener_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "listener_interface",
    hdrs = ["listener.h"],
    deps = [
        "//include/envoy/network:listen_socket_interface",
    ],
)
