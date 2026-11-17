load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {"build": "default"},
        "coverage": {
            "build": "default",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "default",
            "link": "linux_x64_small",
        },
        "asan": {"build": "default"},
    },
    deps = [
        "//source/extensions/upstreams/http/generic:config",
        "//test/mocks:common_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
