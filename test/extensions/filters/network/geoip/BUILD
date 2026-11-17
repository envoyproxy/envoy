load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "geoip_filter_test",
    srcs = ["geoip_filter_test.cc"],
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
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/filters/network/geoip:geoip_filter_lib",
        "//test/extensions/filters/http/geoip:geoip_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/geoip/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
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
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/geoip:config",
        "//test/extensions/filters/http/geoip:geoip_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/geoip/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "geoip_filter_integration_test",
    size = "large",
    srcs = ["geoip_filter_integration_test.cc"],
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    # linux_x64_xlarge: the coverage link peaks at 14870MB,
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
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/network/geoip:config",
        "//source/extensions/filters/network/set_filter_state:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/geoip_providers/maxmind:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/geoip/v3:pkg_cc_proto",
    ],
)
