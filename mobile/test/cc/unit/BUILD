load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_benchmark_binary", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "envoy_config_test",
    srcs = ["envoy_config_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_benchmark_binary(
    name = "envoy_config_benchmark",
    srcs = ["envoy_config_benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_build_config//:extension_registry",
    ],
)
