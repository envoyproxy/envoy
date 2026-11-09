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
    name = "sse_event_codec_test",
    srcs = ["sse_event_codec_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/coroutine:dispatcher_executor_lib",
        "//source/common/coroutine:launch_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager/sse:sse_event_codec_lib",
        "//source/extensions/filters/http/ai_protocol_manager/sse:sse_scanner_lib",
        "//test/extensions/filters/http/ai_protocol_manager:fake_bridge_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@nlohmann_json//:json",
    ],
)
