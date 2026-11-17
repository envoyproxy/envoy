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
    name = "on_demand_filter_test",
    srcs = ["on_demand_filter_test.cc"],
    extension_names = ["envoy.filters.http.on_demand"],
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
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "on_demand_integration_test",
    srcs = ["on_demand_integration_test.cc"],
    extension_names = ["envoy.filters.http.on_demand"],
    # linux_x64_xlarge: the coverage link peaks at 15646MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {
            "build": "linux_x64_small",
            "test": "linux_x64_large",
        },
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_large",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
            "test": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_large",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_large",
        },
    },
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/filters/http/on_demand:config",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration:scoped_rds_lib",
        "//test/integration:vhds_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "odcds_integration_test",
    size = "large",
    srcs = ["odcds_integration_test.cc"],
    extension_names = ["envoy.filters.http.on_demand"],
    # linux_x64_xlarge: the coverage link peaks at 15649MB,
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
        "//source/common/common:macros",
        "//source/extensions/filters/http/on_demand:config",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:ads_integration_lib",
        "//test/integration:ads_xdstp_config_sources_integration_lib",
        "//test/integration:fake_upstream_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:scoped_rds_lib",
        "//test/integration:xdstp_config_sources_integration_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/on_demand/v3:pkg_cc_proto",
    ],
)
