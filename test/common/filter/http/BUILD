load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_config_discovery_impl_test",
    srcs = ["filter_config_discovery_impl_test.cc"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/filter/http:filter_config_discovery_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/http/health_check:config",
        "//source/extensions/filters/http/router:config",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
