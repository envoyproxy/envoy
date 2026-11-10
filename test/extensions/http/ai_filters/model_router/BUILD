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
    extension_names = ["envoy.http.ai_filters.model_router"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/http/ai_filters/model_router:config",
        "//source/extensions/http/ai_filters/model_router:filter_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/ai/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.http.ai_filters.model_router"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_state_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/http/ai_filters/model_router:filter_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "model_router_integration_test",
    size = "large",
    srcs = ["model_router_integration_test.cc"],
    extension_names = [
        "envoy.filters.http.ai_protocol_manager",
        "envoy.http.ai_filters.model_router",
        "envoy.http.ai_filters.request_info",
        "envoy.http.ai_filters.route_selector",
        "envoy.http.ai_filters.schema_validation",
        "envoy.http.ai_filters.transcoder",
    ],
    rbe_pool = {
        "tsan": {"build": "linux_x64_small"},
        "gcc": {"build": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:config",
        "//source/extensions/http/ai_filters/model_router:config",
        "//source/extensions/http/ai_filters/request_info:config",
        "//source/extensions/http/ai_filters/route_selector:config",
        "//source/extensions/http/ai_filters/schema_validation:config",
        "//source/extensions/http/ai_filters/transcoder:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/ai/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)
