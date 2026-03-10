load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
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
    extension_names = ["envoy.load_balancing_policies.override_host"],
    rbe_pool = "6gig",
    deps = [
        ":test_lb",
        ":test_lb_proto_cc_proto",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/load_balancing_policies/override_host:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/override_host/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "load_balancer_test",
    srcs = ["load_balancer_test.cc"],
    extension_names = ["envoy.load_balancing_policies.override_host"],
    deps = [
        ":test_lb",
        ":test_lb_proto_cc_proto",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/extensions/load_balancing_policies/override_host:config",
        "//source/extensions/load_balancing_policies/override_host:load_balancer",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/override_host/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.override_host"],
    deps = [
        ":test_lb",
        ":test_lb_proto_cc_proto",
        "//envoy/http:codec_interface",
        "//envoy/http:header_map_interface",
        "//envoy/network:address_interface",
        "//source/extensions/filters/http/set_metadata:config",
        "//source/extensions/load_balancing_policies/override_host:config",
        "//test/integration:fake_upstream_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:http_integration_lib_light",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "test_lb_proto",
    srcs = ["test_lb.proto"],
)

envoy_cc_test_library(
    name = "test_lb",
    srcs = ["test_lb.cc"],
    hdrs = ["test_lb.h"],
    deps = [
        ":test_lb_proto_cc_proto",
        "//envoy/common:optref_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/common:time_interface",
        "//envoy/registry",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:thread_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
