load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "config_tracker_mocks",
    srcs = ["config_tracker.cc"],
    hdrs = ["config_tracker.h"],
    deps = [
        "//include/envoy/server:configuration_interface",
    ],
)

envoy_cc_mock(
    name = "admin_mocks",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        "//include/envoy/server:admin_interface",
        "//test/mocks/server:config_tracker_mocks",
    ],
)

envoy_cc_mock(
    name = "server_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:bootstrap_extension_config_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:health_checker_config_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/server:tracer_config_interface",
        "//include/envoy/server:worker_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/upstream:health_checker_interface",
        "//source/common/grpc:context_lib",
        "//source/common/http:context_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:config_tracker_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)
