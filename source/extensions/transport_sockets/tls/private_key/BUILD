load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "private_key_manager_lib",
    srcs = [
        "private_key_manager_impl.cc",
    ],
    hdrs = [
        "private_key_manager_impl.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/registry:registry_no_include_prefix",
        "//include/envoy/ssl/private_key:private_key_config_interface_no_include_prefix",
        "//include/envoy/ssl/private_key:private_key_interface_no_include_prefix",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
