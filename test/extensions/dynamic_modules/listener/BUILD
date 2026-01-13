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
        "//test/extensions/dynamic_modules/test_data/c:listener_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
        "//test/extensions/dynamic_modules/test_data/c:listener_stop_iteration",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/listener/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:listener_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/listener/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/network:address_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/listener/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
    ],
)
