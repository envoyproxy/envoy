load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "config_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/config:config_provider_manager_interface",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/config:config_provider_lib",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "custom_config_validators_mocks",
    srcs = ["custom_config_validators.cc"],
    hdrs = ["custom_config_validators.h"],
    deps = [
        "//envoy/config:config_validator_interface",
    ],
)

envoy_cc_mock(
    name = "eds_resources_cache_mocks",
    srcs = ["eds_resources_cache.cc"],
    hdrs = ["eds_resources_cache.h"],
    deps = [
        "//envoy/config:eds_resources_cache_interface",
        "//source/common/common:logger_lib",
    ],
)
