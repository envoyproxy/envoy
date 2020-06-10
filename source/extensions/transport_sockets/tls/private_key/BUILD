licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "private_key_manager_lib",
    srcs = [
        "private_key_manager_impl.cc",
    ],
    hdrs = [
        "private_key_manager_impl.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/registry",
        "//include/envoy/ssl/private_key:private_key_config_interface",
        "//include/envoy/ssl/private_key:private_key_interface",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
