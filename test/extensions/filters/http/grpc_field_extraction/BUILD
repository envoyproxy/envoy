load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = ["//test/proto:apikeys_proto_descriptor"],
    deps = [
        "//source/extensions/filters/http/grpc_field_extraction:extractor",
        "//source/extensions/filters/http/grpc_field_extraction:filter",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/mocks/http:http_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "filter_config_test",
    srcs = ["filter_config_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/proto:apikeys_proto_descriptor",
    ],
    deps = [
        "//source/extensions/filters/http/grpc_field_extraction:extractor",
        "//source/extensions/filters/http/grpc_field_extraction:extractor_impl",
        "//source/extensions/filters/http/grpc_field_extraction:filter_config",
        "//test/mocks/http:http_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = [
        "integration_test.cc",
    ],
    data = ["//test/proto:apikeys_proto_descriptor"],
    extension_names = ["envoy.filters.http.grpc_field_extraction"],
    deps = [
        "//source/extensions/filters/http/grpc_field_extraction:config",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "@com_google_absl//absl/strings:str_format",
    ],
)
