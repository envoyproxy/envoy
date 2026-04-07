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

# Common deps for linking the Hickory DNS Rust static library. The Rust static library
# includes the full SDK, which references ABI callbacks from all dynamic module extension
# types. These abi_impl deps provide the callback implementations needed at link time.
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

envoy_extension_cc_test(
    name = "hickory_dns_impl_test",
    srcs = ["hickory_dns_impl_test.cc"],
    extension_names = ["envoy.network.dns_resolver.hickory"],
    rbe_pool = "6gig",
    tags = [
        # It is a known issue that TSAN detects a false positive in uninstrumented Rust code:
        # https://github.com/rust-lang/rust/issues/39608
        # The Rust standard library and Tokio runtime are not compiled with -fsanitize=thread,
        # so TSAN cannot see their internal atomic synchronization. Building Rust with
        # -Zsanitizer=thread requires nightly and causes symbol conflicts with Bazel's sanitizer,
        # and -Zbuild-std is not supported by rules_rust:
        # https://github.com/bazelbuild/rules_rust/issues/2068
        # ASAN works without any issue.
        "no_tsan",
    ],
    deps = [
        "//source/extensions/network/dns_resolver/hickory:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/network/dns_resolver/hickory/v3:pkg_cc_proto",
    ] + HICKORY_ABI_IMPL_DEPS,
)

envoy_extension_cc_test(
    name = "hickory_dns_integration_test",
    srcs = ["hickory_dns_integration_test.cc"],
    extension_names = ["envoy.network.dns_resolver.hickory"],
    rbe_pool = "6gig",
    tags = [
        # See comment on hickory_dns_impl_test above.
        "no_tsan",
    ],
    deps = [
        "//source/extensions/clusters/dns:dns_cluster_lib",
        "//source/extensions/network/dns_resolver/hickory:config",
        "//test/integration:http_integration_lib",
    ] + HICKORY_ABI_IMPL_DEPS,
)
