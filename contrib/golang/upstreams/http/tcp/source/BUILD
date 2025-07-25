load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":upstream_request_lib",
        "@envoy_api//contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_request_lib",
    srcs = [
        "processor_state.cc",
        "upstream_request.cc",
    ],
    hdrs = [
        "processor_state.h",
        "upstream_request.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":cgo",
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:router_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
        "@envoy_api//contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "cgo",
    srcs = ["cgo.cc"],
    hdrs = [
        "processor_state.h",
        "upstream_request.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//contrib/golang/common/log:log_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/memory:utils_lib",
        "//source/common/network:connection_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/upstreams/http/tcp:upstream_request_lib",
        "@envoy_api//contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
