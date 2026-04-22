load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "json_rpc_decoder_lib",
    hdrs = ["json_rpc_decoder.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

# Stage 1: generic JSON SAX decoder interface.
envoy_cc_library(
    name = "json_decoder_lib",
    hdrs = ["json_decoder.h"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)

# Stage 1: streaming JSON parser that fires JsonDecoder events.
envoy_cc_library(
    name = "json_parser_lib",
    srcs = ["json_parser.cc"],
    hdrs = ["json_parser.h"],
    deps = [
        ":json_decoder_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:logger_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# Stage 2: maps JsonDecoder events onto JsonRpcDecoder callbacks.
envoy_cc_library(
    name = "json_rpc_translator_lib",
    hdrs = ["json_rpc_translator.h"],
    deps = [
        ":json_decoder_lib",
        ":json_rpc_decoder_lib",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

# Convenience facade composing JsonParser + JsonRpcTranslator.
# Keeps the original JsonRpcParser API intact for existing callers.
envoy_cc_library(
    name = "json_rpc_parser_lib",
    srcs = ["json_rpc_parser.cc"],
    hdrs = ["json_rpc_parser.h"],
    deps = [
        ":json_decoder_lib",
        ":json_parser_lib",
        ":json_rpc_decoder_lib",
        ":json_rpc_translator_lib",
        "//envoy/buffer:buffer_interface",
        "@abseil-cpp//absl/status",
    ],
)

envoy_cc_library(
    name = "json_rpc_connection_manager_lib",
    srcs = ["json_rpc_connection_manager.cc"],
    hdrs = ["json_rpc_connection_manager.h"],
    deps = [
        ":json_decoder_lib",
        ":json_parser_lib",
        ":json_rpc_decoder_lib",
        ":json_rpc_translator_lib",
        "//envoy/http:filter_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
