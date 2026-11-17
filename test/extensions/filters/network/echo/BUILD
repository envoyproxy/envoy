load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    # linux_x64_xlarge: the coverage link peaks at 14721MB,
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
    tags = [
        # Uncomment this line to run this test repeatedly in exclusive mode if not using docker-sandbox,
        # or RBE, see comments in AddRemoveListener.
        # "exclusive",
        "cpu:2",
    ],
    deps = [
        "//source/extensions/filters/network/echo:config",
        "//test/integration:integration_lib",
        "//test/server:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
