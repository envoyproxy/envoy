load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        "//envoy/router:router_interface",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_cc_library(
    name = "route_matcher",
    srcs = ["route_matcher.cc"],
    hdrs = ["route_matcher.h"],
    deps = [
        ":router_interface",
        "//envoy/router:router_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy:serializer_interface",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":router_lib",
        "//envoy/rds:rds_config_interface",
        "//envoy/registry",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/router/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network/dubbo_proxy:app_exception_lib",
        "//source/extensions/filters/network/dubbo_proxy:protocol_interface",
        "//source/extensions/filters/network/dubbo_proxy:serializer_interface",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "rds_interface",
    hdrs = ["rds.h"],
    deps = [
        ":router_interface",
        "//envoy/rds:rds_interface",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    hdrs = [
        "rds_impl.h",
    ],
    deps = [
        ":rds_interface",
        ":route_matcher",
        "//envoy/rds:rds_interface",
        "//source/common/rds:rds_lib",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)
