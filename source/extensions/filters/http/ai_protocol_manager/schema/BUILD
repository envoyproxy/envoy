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
