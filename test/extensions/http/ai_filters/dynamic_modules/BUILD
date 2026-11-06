load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "json_pointer_test",
    srcs = ["json_pointer_test.cc"],
    extension_names = ["envoy.http.ai_filters.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/http/ai_filters/dynamic_modules:json_pointer_lib",
        "//test/test_common:status_utility_lib",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "ai_filter_test",
    srcs = ["ai_filter_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:ai_filter",
        "//test/extensions/dynamic_modules/test_data/c:ai_filter_missing_request",
    ],
    extension_names = ["envoy.http.ai_filters.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/http/ai_filters/dynamic_modules:ai_filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:ai_filter",
        "//test/extensions/dynamic_modules/test_data/c:ai_filter_missing_request",
    ],
    extension_names = ["envoy.http.ai_filters.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/registry",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/http/ai_filters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:ai_filter",
        "//test/extensions/dynamic_modules/test_data/rust:ai_filter_integration_test",
    ],
    extension_names = [
        "envoy.filters.http.ai_protocol_manager",
        "envoy.http.ai_filters.dynamic_modules",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:config",
        "//source/extensions/http/ai_filters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/ai/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)
