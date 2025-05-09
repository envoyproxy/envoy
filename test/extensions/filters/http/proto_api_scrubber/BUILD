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
    name = "integration_test",
    size = "large",
    srcs = [
        "integration_test.cc",
    ],
    data = ["//test/proto:apikeys_proto_descriptor"],
    extension_names = ["envoy.filters.http.proto_api_scrubber"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/proto_api_scrubber:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "@com_google_absl//absl/strings:str_format",
    ],
)
