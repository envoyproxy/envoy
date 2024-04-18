load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "custom_static_cluster",
    srcs = [
        "custom_static_cluster.cc",
    ],
    hdrs = [
        "custom_static_cluster.h",
    ],
    deps = [
        ":cluster_factory_config_proto_cc_proto",
        "//envoy/api:api_interface",
        "//envoy/http:codec_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration/load_balancers:custom_lb_policy",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "cluster_factory_config_proto",
    srcs = [":cluster_factory_config.proto"],
)
