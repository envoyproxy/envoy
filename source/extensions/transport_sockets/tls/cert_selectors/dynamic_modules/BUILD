load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tls_certificate_selector_lib",
    srcs = [
        "abi_impl.cc",
        "tls_certificate_selector.cc",
    ],
    hdrs = ["tls_certificate_selector.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/common:optref_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:metric_registry",
        "//source/extensions/dynamic_modules/abi",
        "//source/extensions/transport_sockets/tls/cert_selectors/common:async_context_lib",
        "//source/server:generic_factory_context_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
    # Force the ABI callback definitions in abi_impl.cc to be linked and exported so a module that
    # calls them resolves the real implementations rather than the fail closed weak stubs, matching
    # the other dynamic module abi_impl targets.
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tls_certificate_selector_lib",
        "//envoy/registry",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:handshaker_interface",
        "//source/common/common:thread_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3:pkg_cc_proto",
    ],
)
