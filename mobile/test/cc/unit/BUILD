load("@envoy//bazel:envoy_build_system.bzl", "envoy_benchmark_test", "envoy_cc_benchmark_binary", "envoy_cc_test", "envoy_mobile_package")
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_yaml")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "envoy_config_test",
    srcs = envoy_select_enable_yaml(
        ["envoy_config_test.cc"],
        "@envoy",
    ),
    linkopts = select({
        "@envoy//bazel:apple": [
            # For the TestProxyResolutionApi test.
            "-Wl,-framework,CoreFoundation",
            "-Wl,-framework,CFNetwork",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "request_headers_builder_test",
    srcs = ["request_headers_builder_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_test(
    name = "fetch_client_test",
    srcs = ["fetch_client_test.cc"],
    repository = "@envoy",
    deps = [
        "//examples/cc/fetch_client:fetch_client_lib",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_benchmark_binary(
    name = "fetch_client_benchmark",
    srcs = ["fetch_client_benchmark_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//examples/cc/fetch_client:fetch_client_lib",
        "@envoy_build_config//:extension_registry",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_benchmark_test(
    name = "fetch_client_benchmark_test",
    benchmark_binary = "fetch_client_benchmark",
)
