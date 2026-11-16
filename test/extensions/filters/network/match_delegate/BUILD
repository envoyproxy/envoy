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
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
    },
    deps = [
        "//source/extensions/filters/network/match_delegate:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "match_delegate_integration_test",
    srcs = ["match_delegate_integration_test.cc"],
    # linux_x64_xlarge: the coverage link peaks at 11177MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "openssl": {"link": "linux_x64_small"},
        "coverage": {"link": "linux_x64_xlarge"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/network/match_delegate:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/server/config_validation:server_lib",
        "//test/config:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
