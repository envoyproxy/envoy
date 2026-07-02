load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_session_no_op",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:udp_session_no_op",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_session_no_op",
    ],
    deps = [
        "//envoy/network:filter_interface",
        "//source/common/stats:custom_stat_namespaces_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/dynamic_modules/v3:pkg_cc_proto",
    ],
)
