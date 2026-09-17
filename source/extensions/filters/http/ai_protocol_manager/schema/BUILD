load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "gemini_generate_content_lib",
    srcs = ["gemini_generate_content.cc"],
    hdrs = ["gemini_generate_content.h"],
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:schema_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "openai_chat_completions_lib",
    srcs = ["openai_chat_completions.cc"],
    hdrs = ["openai_chat_completions.h"],
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:schema_lib",
    ],
)
