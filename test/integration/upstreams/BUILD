load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "per_host_upstream_config",
    hdrs = [
        "per_host_upstream_config.h",
    ],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:filter_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/upstreams/http/http:upstream_request_lib",
        "//source/extensions/upstreams/http/tcp:upstream_request_lib",
    ],
)
