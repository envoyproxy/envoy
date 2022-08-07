load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tracer_interface",
    hdrs = ["tracer.h"],
    deps = [
        "//source/common/http:conn_manager_config_interface",
    ],
)

envoy_cc_library(
    name = "tracer_lib",
    srcs = ["tracer_impl.cc"],
    hdrs = ["tracer_impl.h"],
    deps = [
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:conn_manager_config_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tracer_lib",
        "//envoy/registry",
        "//envoy/singleton:instance_interface",
        "//source/common/tracing:custom_tag_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:http_tracer_manager_lib",
        "//source/common/tracing:tracer_config_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "skywalking_specialization",
    srcs = ["skywalking_specialization.cc"],
    hdrs = ["skywalking_specialization.h"],
    external_deps = [
        "cpp2sky",
    ],
    deps = [
        "//envoy/tracing:trace_driver_interface",
        "//source/extensions/tracers/skywalking:skywalking_tracer_lib",
    ],
)

envoy_cc_library(
    name = "tracer_util",
    srcs = ["tracer_util.cc"],
    hdrs = ["tracer_util.h"],
    deps = [
        ":skywalking_specialization",
        "//envoy/tracing:trace_driver_interface",
        "//source/extensions/request_id/uuid:config",
        "//source/extensions/filters/network/dubbo_proxy:message_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)
