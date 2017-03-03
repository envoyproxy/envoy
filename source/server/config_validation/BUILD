package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "api_lib",
    srcs = ["api.cc"],
    hdrs = ["api.h"],
    deps = [
        ":dispatcher_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/api:api_lib",
    ],
)

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client.cc"],
    hdrs = ["async_client.h"],
    deps = [
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:message_interface",
    ],
)

envoy_cc_library(
    name = "cluster_manager_lib",
    srcs = ["cluster_manager.cc"],
    hdrs = ["cluster_manager.h"],
    deps = [
        ":async_client_lib",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/upstream:cluster_manager_lib",
    ],
)

envoy_cc_library(
    name = "connection_handler_lib",
    srcs = ["connection_handler.cc"],
    hdrs = ["connection_handler.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/network:connection_handler_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
    ],
)

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = ["dispatcher.cc"],
    hdrs = ["dispatcher.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/event:dispatcher_lib",
    ],
)

envoy_cc_library(
    name = "dns_lib",
    srcs = ["dns.cc"],
    hdrs = ["dns.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:dns_interface",
    ],
)

envoy_cc_library(
    name = "hot_restart_lib",
    hdrs = ["hot_restart.h"],
    deps = [
        "//include/envoy/server:hot_restart_interface",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "server_lib",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    deps = [
        ":api_lib",
        ":cluster_manager_lib",
        ":connection_handler_lib",
        ":dns_lib",
        "//include/envoy/common:optional",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/ssl:context_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/server:configuration_lib",
        "//source/server:server_lib",
        "//source/server/http:admin_lib",
    ],
)
