load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

#envoy_package()

envoy_extension_package()

envoy_cc_extension(
    name = "per_host_upstream_config",
    srcs = [
        "per_host_upstream_config.cc",
    ],
    hdrs = [
        "per_host_upstream_config.h",
    ],
    security_posture = "robust_to_untrusted_downstream",
    visibility = ["//visibility:public"],
    deps = [
        ":per_host_upstream_request_lib",
        "//source/extensions/upstreams/http/http:upstream_request_lib",
        "//source/extensions/upstreams/http/tcp:upstream_request_lib",
    ],
)

envoy_cc_library(
    name = "per_host_upstream_request_lib",
    srcs = [
        "per_host_upstream_request.cc",
    ],
    hdrs = [
        "per_host_upstream_request.h",
    ],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/upstreams/http/http:upstream_request_lib",
    ],
)
