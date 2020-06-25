load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":router_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:well_known_names",
        "@envoy_api//envoy/config/filter/thrift/router/v2alpha1:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/router:router_interface",
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_interface",
    hdrs = ["router_ratelimit.h"],
    deps = [
        ":router_interface",
        "//include/envoy/router:router_ratelimit_interface",
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        ":router_ratelimit_lib",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:load_balancer_interface",
        "//include/envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_converter_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_interface",
        "//source/extensions/filters/network/thrift_proxy:thrift_object_interface",
        "//source/extensions/filters/network/thrift_proxy:transport_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_lib",
    srcs = ["router_ratelimit_impl.cc"],
    hdrs = ["router_ratelimit_impl.h"],
    deps = [
        ":router_interface",
        ":router_ratelimit_interface",
        "//source/common/http:header_utility_lib",
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
