load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "cache_file_fixed_block_test",
    srcs = ["cache_file_fixed_block_test.cc"],
    deps = [
        "//source/extensions/http/cache/file_system_http_cache:cache_file_fixed_block",
    ],
)
