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
    name = "utils_test",
    srcs = ["utils_test.cc"],
    extension_names = ["envoy.filters.http.grpc_json_reverse_transcoder"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/grpc_json_reverse_transcoder:utils",
        "@com_github_nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        "//test/proto:bookstore.proto",
        "//test/proto:bookstore_proto_descriptor",
    ],
    extension_names = ["envoy.filters.http.grpc_json_reverse_transcoder"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/grpc_json_reverse_transcoder:filter",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.grpc_json_reverse_transcoder"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/grpc_json_reverse_transcoder:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "reverse_transcoder_integration_test",
    size = "large",
    srcs = [
        "grpc_json_reverse_transcoder_integration_test.cc",
    ],
    data = [
        "//test/proto:bookstore_proto_descriptor",
    ],
    extension_names = ["envoy.filters.http.grpc_json_reverse_transcoder"],
    rbe_pool = "4core",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/common/grpc:codec_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_json_reverse_transcoder:config",
        "//test/integration:http_integration_lib",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/upstream/local_address_selector/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3:pkg_cc_proto",
    ],
)
