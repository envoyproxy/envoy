load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "curl_test",
    srcs = ["curl_test.cc"],
    rbe_pool = "2core",
    external_deps = ["curl"],
)
