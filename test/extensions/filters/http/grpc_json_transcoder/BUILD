licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "json_transcoder_filter_test",
    srcs = ["json_transcoder_filter_test.cc"],
    data = [
        "//test/proto:bookstore.proto",
        "//test/proto:bookstore_proto_descriptor",
    ],
    extension_name = "envoy.filters.http.grpc_json_transcoder",
    deps = [
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:json_transcoder_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/proto:bookstore_proto_cc",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "transcoder_input_stream_test",
    srcs = ["transcoder_input_stream_test.cc"],
    extension_name = "envoy.filters.http.grpc_json_transcoder",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:transcoder_input_stream_lib",
    ],
)

envoy_extension_cc_test(
    name = "grpc_json_transcoder_integration_test",
    srcs = [
        "grpc_json_transcoder_integration_test.cc",
    ],
    data = [
        "//test/proto:bookstore_proto_descriptor",
    ],
    extension_name = "envoy.filters.http.grpc_json_transcoder",
    deps = [
        "//source/common/grpc:codec_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:config",
        "//test/integration:http_integration_lib",
        "//test/proto:bookstore_proto_cc",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.grpc_json_transcoder",
    deps = [
        "//source/extensions/filters/http/grpc_json_transcoder:config",
        "//test/mocks/server:server_mocks",
    ],
)
