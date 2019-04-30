licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
        ":conn_pool_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:address_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/common/redis:client_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        ":conn_pool_interface",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:to_lower_table_lib",
        ":conn_pool_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
    ],
)
