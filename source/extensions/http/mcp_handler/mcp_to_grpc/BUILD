load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)
 
licenses(["notice"])  # Apache 2
 
envoy_extension_package()
 
envoy_cc_library(
    name = "mcp_to_grpc_lib",
    srcs = ["mcp_to_grpc.cc"],
    hdrs = ["mcp_to_grpc.h"],
    external_deps = [
        "path_matcher",
        "grpc_transcoding",
        "http_api_protos",
    ],
    deps = [
        ":transcoder_input_stream_lib",
        "//envoy/http:filter_interface",
        "//envoy/http:mcp_handler_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_nlohmann_json//:json",
        "@com_google_googleapis//google/api:http_cc_proto",
        "@com_google_googleapis//google/api:field_behavior_cc_proto",
        "@envoy_api//envoy/extensions/http/mcp_handler/mcp_to_grpc/v3:pkg_cc_proto",
    ],
)
 
envoy_cc_library(
    name = "transcoder_input_stream_lib",
    srcs = ["transcoder_input_stream_impl.cc"],
    hdrs = ["transcoder_input_stream_impl.h"],
    external_deps = ["grpc_transcoding"],
    deps = ["//source/common/buffer:zero_copy_input_stream_lib"],
)
 
envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mcp_to_grpc_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//envoy/http:mcp_handler_interface",
        "@envoy_api//envoy/extensions/http/mcp_handler/mcp_to_grpc/v3:pkg_cc_proto",
    ],
)
