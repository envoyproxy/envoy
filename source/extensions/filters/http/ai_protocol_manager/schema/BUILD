load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "openai_chat_completions_lib",
    srcs = ["openai_chat_completions.cc"],
    hdrs = ["openai_chat_completions.h"],
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:schema_lib",
    ],
)

envoy_cc_library(
    name = "schema_registry_lib",
    srcs = ["schema_registry.cc"],
    hdrs = ["schema_registry.h"],
    deps = [
        ":openai_chat_completions_lib",
        "//source/extensions/filters/http/ai_protocol_manager:schema_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_extractor_lib",
    ],
)
