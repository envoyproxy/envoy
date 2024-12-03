load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":router_lib",
        "//envoy/registry",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/router/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        "//envoy/rds:rds_interface",
        "//envoy/router:router_interface",
        "//envoy/tcp:conn_pool_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_converter_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_options_config_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_interface",
    hdrs = ["router_ratelimit.h"],
    deps = [
        ":router_interface",
        "//envoy/router:router_ratelimit_interface",
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
    ],
)

envoy_cc_library(
    name = "upstream_request_lib",
    srcs = ["upstream_request.cc"],
    hdrs = ["upstream_request.h"],
    deps = [
        ":router_interface",
        "//envoy/tcp:conn_pool_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy:thrift_object_interface",
        "//source/extensions/filters/network/thrift_proxy:transport_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "shadow_writer_lib",
    srcs = ["shadow_writer_impl.cc"],
    hdrs = ["shadow_writer_impl.h"],
    deps = [
        ":router_interface",
        ":upstream_request_lib",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:linked_object",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_converter_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_interface",
        "//source/extensions/filters/network/thrift_proxy:thrift_object_interface",
        "//source/extensions/filters/network/thrift_proxy:transport_interface",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        ":router_ratelimit_lib",
        ":shadow_writer_lib",
        ":upstream_request_lib",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
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

envoy_cc_library(
    name = "rds_interface",
    hdrs = ["rds.h"],
    deps = [
        ":router_interface",
        "//envoy/rds:rds_interface",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    hdrs = [
        "rds_impl.h",
    ],
    deps = [
        ":config",
        ":rds_interface",
        "//envoy/rds:rds_interface",
        "//source/common/rds:rds_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)
