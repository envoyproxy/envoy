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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.http.ai_filters.transcoder"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/ai_protocol_manager:transcoding_engine_lib",
        "//source/extensions/http/ai_filters/transcoder:filter_lib",
        "//test/extensions/filters/http/ai_protocol_manager:fake_bridge_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/transcoder/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "golden_test",
    srcs = ["golden_test.cc"],
    data = ["testdata/response_goldens.json"],
    extension_names = ["envoy.http.ai_filters.transcoder"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:transcoding_engine_lib",
        "//source/extensions/http/ai_filters/transcoder:filter_lib",
        "//test/extensions/filters/http/ai_protocol_manager:fake_bridge_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/transcoder/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.ai_filters.transcoder"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/http/ai_filters/transcoder:config",
        "//source/extensions/http/ai_filters/transcoder:filter_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/transcoder/v3:pkg_cc_proto",
    ],
)
