licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "admin_lib",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        "//include/envoy/server:admin_interface",
        "//source/common/common:assert_lib",
        "//source/server/http:config_tracker_lib",
    ],
)

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
        ":dispatcher_lib",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:message_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "cluster_manager_lib",
    srcs = ["cluster_manager.cc"],
    hdrs = ["cluster_manager.h"],
    deps = [
        ":async_client_lib",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:utility_lib",
        "//source/common/upstream:cluster_manager_lib",
    ],
)

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = ["dispatcher.cc"],
    hdrs = [
        "connection.h",
        "dispatcher.h",
        "dns.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//source/common/common:assert_lib",
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
    name = "server_lib",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":admin_lib",
        ":api_lib",
        ":cluster_manager_lib",
        ":dns_lib",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/common:version_lib",
        "//source/common/config:bootstrap_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/ssl:context_lib",
        "//source/common/stats:stats_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/server:configuration_lib",
        "//source/server:server_lib",
        "//source/server/http:admin_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)
