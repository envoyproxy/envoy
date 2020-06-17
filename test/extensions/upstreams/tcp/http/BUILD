load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "http_upstream_test",
    srcs = ["http_upstream_test.cc"],
    deps = [
        "//include/envoy/tcp:upstream_interface",
        "//source/extensions/upstreams/tcp/http:http_upstream",
        "//test/mocks/http:http_mocks",
        "//test/mocks/tcp:tcp_mocks",
    ],
)
