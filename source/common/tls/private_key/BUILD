load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

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
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
