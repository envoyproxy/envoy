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
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    extension_names = ["envoy.upstreams.http.http"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/upstreams/http/http:upstream_request_lib",
        "//test/mocks/http:stream_encoder_mock",
        "//test/mocks/router:router_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)
