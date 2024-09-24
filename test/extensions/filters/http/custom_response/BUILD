load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    extension_names = ["envoy.filters.http.custom_response"],
    deps = [
        "//envoy/http:filter_interface",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:policy_interface",
        "//source/extensions/http/custom_response/redirect_policy:redirect_policy_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/custom_response/local_response_policy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/custom_response/redirect_policy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.custom_response"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/custom_response:config",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:factory",
        "//source/extensions/http/custom_response/local_response_policy:local_response_policy_lib",
        "//source/extensions/http/custom_response/redirect_policy:redirect_policy_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "custom_response_filter_test",
    size = "small",
    srcs = ["custom_response_filter_test.cc"],
    extension_names = ["envoy.filters.http.custom_response"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/custom_response:config",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:factory",
        "//source/extensions/http/custom_response/local_response_policy:local_response_policy_lib",
        "//source/extensions/http/custom_response/redirect_policy:redirect_policy_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "custom_response_integration_test",
    size = "large",
    srcs = [
        "custom_response_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.custom_response"],
    rbe_pool = "2core",
    shard_count = 2,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/custom_response:factory",
        "//source/extensions/http/custom_response/local_response_policy:local_response_policy_lib",
        "//source/extensions/http/custom_response/redirect_policy:redirect_policy_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration/filters:common_lib",
        "//test/integration/filters:local_reply_during_decoding_filter_lib",
        "//test/integration/filters:local_reply_during_encoding_filter_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
