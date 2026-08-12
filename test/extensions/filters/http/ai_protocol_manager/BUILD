load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "external_buffer_impl_test",
    srcs = ["external_buffer_impl_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "json_with_ext_buf_test",
    srcs = ["json_with_ext_buf_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "json_with_ext_buf_parser_test",
    srcs = ["json_with_ext_buf_parser_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_parser_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "buffer_manager_test",
    srcs = ["buffer_manager_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "token_usage_extractor_test",
    srcs = ["token_usage_extractor_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_extractor_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "response_handler_test",
    srcs = ["response_handler_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:response_handler_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/http:codes_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_impl_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_chain_bridge_test",
    srcs = ["filter_chain_bridge_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:filter_chain_bridge_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "response_handler_benchmark",
    srcs = ["response_handler_benchmark.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:response_handler_lib",
        "//test/common/stats:stat_test_utility_lib",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "response_handler_benchmark_test",
    benchmark_binary = "response_handler_benchmark",
)

envoy_cc_fuzz_test(
    name = "response_handler_fuzz_test",
    srcs = ["response_handler_fuzz_test.cc"],
    corpus = "response_handler_corpus",
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_protocol_manager:response_handler_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ai_protocol_manager_integration_test",
    size = "large",
    srcs = ["ai_protocol_manager_integration_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "6gig",
    shard_count = 4,
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:config",
        # A downstream buffer filter is used to trip a local reply during replay.
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/extensions/filters/http/ext_proc:test_processor_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)
