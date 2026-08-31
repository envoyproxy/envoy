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
    name = "openai_chat_completions_test",
    srcs = ["openai_chat_completions_test.cc"],
    extension_names = ["envoy.filters.http.ai_protocol_manager"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/ai_protocol_manager/schema:openai_chat_completions_lib",
        "//test/test_common:status_utility_lib",
        "@nlohmann_json//:json",
    ],
)
