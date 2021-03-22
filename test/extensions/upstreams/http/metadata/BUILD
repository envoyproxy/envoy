load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(tbarrella): Use envoy_extension_cc_test once config.cc exists.
envoy_cc_test(
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    deps = [
        "//source/extensions/upstreams/http/metadata:upstream_request_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:stream_encoder_mock",
        "//test/mocks/router:router_mocks",
        "//test/test_common:utility_lib",
    ],
)
