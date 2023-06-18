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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/http/header_to_metadata:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "subset_test",
    srcs = ["subset_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
    ],
)
