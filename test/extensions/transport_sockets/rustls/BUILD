load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Common deps for linking the rustls kTLS Rust static library. The Rust static library
# includes the full SDK, which references ABI callbacks from all dynamic module extension
# types. These abi_impl deps provide the callback implementations needed at link time.
RUSTLS_ABI_IMPL_DEPS = [
    "//source/extensions/access_loggers/dynamic_modules:access_log_lib",
    "//source/extensions/bootstrap/dynamic_modules:abi_impl",
    "//source/extensions/clusters/dynamic_modules:cluster_lib",
    "//source/extensions/dynamic_modules:abi_impl",
    "//source/extensions/filters/http/dynamic_modules:abi_impl",
    "//source/extensions/filters/listener/dynamic_modules:filter_lib",
    "//source/extensions/filters/network/dynamic_modules:filter_lib",
    "//source/extensions/filters/udp/dynamic_modules:filter_lib",
    "//source/extensions/load_balancing_policies/dynamic_modules:abi_impl",
    "//source/extensions/matching/input_matchers/dynamic_modules:matcher_lib",
    "//source/extensions/upstreams/http/dynamic_modules:abi_impl",
]

envoy_extension_cc_test(
    name = "rustls_impl_test",
    srcs = ["rustls_impl_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    extension_names = ["envoy.transport_sockets.rustls"],
    rbe_pool = "6gig",
    tags = [
        # Rust static libraries are not instrumented with TSAN, causing false positives.
        "no_tsan",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/extensions/dynamic_modules/abi",
        "//source/extensions/transport_sockets/rustls:config",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/transport_sockets/rustls/v3:pkg_cc_proto",
    ] + RUSTLS_ABI_IMPL_DEPS,
)

envoy_extension_cc_test(
    name = "rustls_integration_test",
    srcs = ["rustls_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.transport_sockets.rustls"],
    rbe_pool = "6gig",
    tags = [
        # Rust static libraries are not instrumented with TSAN, causing false positives.
        "no_tsan",
    ],
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/rustls:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/extensions/transport_sockets/rustls/v3:pkg_cc_proto",
    ] + RUSTLS_ABI_IMPL_DEPS,
)
