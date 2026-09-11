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
    name = "extractor_test",
    srcs = ["extractor_test.cc"],
    extension_names = ["envoy.filters.ai.request_info"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/ai/request_info:extractor_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_readers_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_lib",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.ai.request_info"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/ai/request_info:filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//test/extensions/filters/http/ai_protocol_manager:fake_bridge_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/ai/request_info/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.ai.request_info"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/ai/request_info:config",
        "//source/extensions/filters/ai/request_info:filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/ai/request_info/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "request_info_integration_test",
    size = "large",
    srcs = ["request_info_integration_test.cc"],
    extension_names = [
        "envoy.filters.ai.request_info",
        "envoy.filters.http.ai_protocol_manager",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/ai/request_info:config",
        "//source/extensions/filters/http/ai_protocol_manager:config",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/extensions/filters/http/ext_proc:test_processor_lib",
        "//test/integration:http_integration_lib",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)
