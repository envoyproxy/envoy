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
    name = "direct_response_integration_test",
    size = "large",
    srcs = [
        "direct_response_integration_test.cc",
    ],
    extension_names = ["envoy.filters.network.direct_response"],
    # linux_x64_xlarge: the coverage link peaks at 14718MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/network/direct_response:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "direct_response_test",
    srcs = ["direct_response_test.cc"],
    extension_names = ["envoy.filters.network.direct_response"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/network/direct_response:filter",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/filters/network/direct_response/v3:pkg_cc_proto",
    ],
)
