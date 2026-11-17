load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_cluster_backcompat_test",
    srcs = ["dns_cluster_backcompat_test.cc"],
    rbe_pool = {
        "default": {"test": "linux_x64_medium"},
        "coverage": {
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
    },
    deps = [
        "//source/extensions/clusters/common:dns_cluster_backcompat_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/common/dns/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dns/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "logical_host_test",
    srcs = ["logical_host_test.cc"],
    rbe_pool = {
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/clusters/common:logical_host_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
    ],
)

envoy_cc_test(
    name = "logical_host_integration_test",
    srcs = ["logical_host_integration_test.cc"],
    # linux_x64_xlarge: the coverage link peaks at 15117MB,
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
        "//source/common/config:utility_lib",
        "//source/extensions/clusters/common:logical_host_lib",
        "//source/extensions/clusters/dns:dns_cluster_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/extensions/clusters/dns/v3:pkg_cc_proto",
    ],
)
