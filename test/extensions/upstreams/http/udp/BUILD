load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//envoy/http:protocol_interface",
        "//envoy/router:router_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/upstreams/http/udp:upstream_request_lib",
        "//test/common/http:common_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_filter_interface",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {"build": "default"},
        "openssl": {"build": "default"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "default",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/upstreams/http/udp:config",
        "//source/extensions/upstreams/http/udp:upstream_request_lib",
        "//test/mocks:common_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
