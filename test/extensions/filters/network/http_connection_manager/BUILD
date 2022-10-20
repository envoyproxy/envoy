load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "config",
    srcs = ["config.proto"],
)

envoy_extension_cc_test_library(
    name = "config_test_base",
    srcs = ["config_test_base.h"],
    extension_names = ["envoy.filters.network.http_connection_manager"],
    deps = [
        ":config_cc_proto",
        "//source/common/filter:config_discovery_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/health_check:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/request_id/uuid:config",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.http_connection_manager"],
    deps = [
        ":config_cc_proto",
        ":config_test_base",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/http/original_ip_detection/custom_header:config",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_filter_chain_test",
    srcs = ["config_filter_chain_test.cc"],
    extension_names = ["envoy.filters.network.http_connection_manager"],
    deps = [
        ":config_cc_proto",
        ":config_test_base",
        "//source/extensions/filters/network/http_connection_manager:config",
    ],
)

envoy_extension_cc_test(
    name = "config_filter_dependencies_test",
    srcs = ["config_filter_dependencies_test.cc"],
    extension_names = ["envoy.filters.network.http_connection_manager"],
    deps = [
        ":config_cc_proto",
        ":config_test_base",
        "//source/extensions/filters/network/http_connection_manager:config",
        "@envoy_api//envoy/extensions/filters/common/dependency/v3:pkg_cc_proto",
    ],
)
