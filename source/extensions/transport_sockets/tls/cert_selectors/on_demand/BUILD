load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:timer_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/router:string_accessor_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/ssl:tls_certificate_config_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:fair_sieve_cache_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_lib",
        "//source/extensions/filters/network/set_filter_state:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/common:async_context_lib",
        "//source/server:generic_factory_context_lib",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
    ],
)
