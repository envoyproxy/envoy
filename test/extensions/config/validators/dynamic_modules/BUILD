load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_dyn_module_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_dyn_module_test(
    name = "config_validator_test",
    srcs = ["config_validator_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:config_validator_test",
    ],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:opaque_resource_decoder_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/config/validators/dynamic_modules:config",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules/abi",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/config/validators/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_dyn_module_test(
    name = "config_validator_integration_test",
    size = "large",
    srcs = ["config_validator_integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:config_validator_test",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/config/validators/dynamic_modules:config",
        "//source/extensions/dynamic_modules:abi_impl",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/config/validators/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
