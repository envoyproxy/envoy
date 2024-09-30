load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "router_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/http:stateful_session_interface",
        "//envoy/json:json_object_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/router:route_config_provider_manager_interface",
        "//envoy/router:router_interface",
        "//envoy/router:router_ratelimit_interface",
        "//envoy/router:scopes_interface",
        "//envoy/router:shadow_writer_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//test/mocks:common_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:host_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "router_filter_interface",
    srcs = ["router_filter_interface.cc"],
    hdrs = ["router_filter_interface.h"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:router_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_mock(
    name = "upstream_request",
    srcs = ["upstream_request.cc"],
    hdrs = ["upstream_request.h"],
    deps = [
        "//source/common/router:router_lib",
    ],
)
