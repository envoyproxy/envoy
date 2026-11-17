load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "local_ratelimit_test",
    srcs = ["local_ratelimit_test.cc"],
    extension_names = ["envoy.filters.network.local_ratelimit"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/filters/network/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "local_ratelimit_integration_test",
    size = "large",
    srcs = ["local_ratelimit_integration_test.cc"],
    extension_names = ["envoy.filters.network.local_ratelimit"],
    # linux_x64_xlarge: the coverage link peaks at 14986MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "openssl": {
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
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/filters/network/local_ratelimit:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_proto_library(
    name = "local_ratelimit_fuzz_proto",
    srcs = ["local_ratelimit_fuzz.proto"],
    deps = [
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "local_ratelimit_fuzz_test",
    srcs = ["local_ratelimit_fuzz_test.cc"],
    corpus = "local_ratelimit_corpus",
    # linux_x64_xlarge: the fuzz coverage link peaks at 11586MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "tsan": {"link": "linux_x64_small"},
        "fuzz_coverage": {"link": "linux_x64_xlarge"},
    },
    deps = [
        ":local_ratelimit_fuzz_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/filters/network/local_ratelimit:local_ratelimit_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg_cc_proto",
    ],
)
