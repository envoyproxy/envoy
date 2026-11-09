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
    name = "sync_filter_test",
    srcs = ["sync_filter_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_request_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/http/ai_filters/common:sync_filter_lib",
        "//test/extensions/filters/http/ai_protocol_manager:fake_bridge_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/status",
        "@nlohmann_json//:json",
    ],
)
