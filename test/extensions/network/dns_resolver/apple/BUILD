load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "apple_dns_impl_test",
    srcs = select({
        "//bazel:apple": ["apple_dns_impl_test.cc"],
        "//conditions:default": [],
    }),
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/network:dns_interface",
        "//source/common/common:random_generator_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ] + select({
        "//bazel:apple": [
            "//source/extensions/network/dns_resolver/apple:config",
        ],
        "//conditions:default": [],
    }),
)
