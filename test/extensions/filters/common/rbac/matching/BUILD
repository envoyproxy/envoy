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

envoy_extension_cc_test(
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/filters/common/rbac/matching:data_impl_lib",
        "//source/extensions/filters/common/rbac/matching:inputs_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "inputs_integration_test",
    srcs = ["inputs_integration_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/filters/common/rbac/matching:data_impl_lib",
        "//source/extensions/filters/common/rbac/matching:inputs_lib",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)
