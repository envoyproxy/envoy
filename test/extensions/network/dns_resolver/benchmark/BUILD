load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# ABI callback implementations required for linking the Hickory DNS Rust static library.
# Mirrors the list in test/extensions/network/dns_resolver/hickory/BUILD.
HICKORY_ABI_IMPL_DEPS = [
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

envoy_cc_benchmark_binary(
    name = "dns_resolver_benchmark",
    srcs = ["dns_resolver_benchmark.cc"],
    rbe_pool = "6gig",
    tags = [
        # FakeUdpDnsServer uses POSIX-only APIs (poll, fcntl, etc.).
        "fails_on_clang_cl",
        # Hickory uses a Rust/Tokio runtime that is not instrumented for TSAN.
        "no_tsan",
    ],
    deps = [
        "//source/common/event:libevent_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/extensions/network/dns_resolver/cares:config",
        "//source/extensions/network/dns_resolver/hickory:config",
        "//test/extensions/network/dns_resolver/common:fake_udp_dns_server_lib",
        "//test/test_common:utility_lib",
        "@benchmark",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/cares/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/hickory/v3:pkg_cc_proto",
    ] + HICKORY_ABI_IMPL_DEPS,
)

envoy_benchmark_test(
    name = "dns_resolver_benchmark_test",
    benchmark_binary = "dns_resolver_benchmark",
    tags = [
        "fails_on_clang_cl",
        "no_tsan",
    ],
)
