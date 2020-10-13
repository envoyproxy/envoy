load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(["example_configs_test_setup.sh"])

# These extensions are used in the example configs we test.
example_configs_extensions = [
    "envoy.clusters.dynamic_forward_proxy",
    "envoy.filters.http.cache",
    "envoy.filters.http.cache.simple_http_cache",
    "envoy.filters.http.cors",
    "envoy.filters.http.csrf",
    "envoy.filters.http.ext_authz",
    "envoy.filters.http.fault",
    "envoy.filters.http.health_check",
    "envoy.filters.http.lua",
    "envoy.filters.http.buffer",
    "envoy.filters.http.dynamo",
    "envoy.filters.http.ratelimit",
    "envoy.filters.http.router",
    "envoy.filters.http.grpc_http1_bridge",
    "envoy.filters.listener.original_dst",
    "envoy.filters.listener.proxy_protocol",
    "envoy.filters.listener.tls_inspector",
    "envoy.filters.network.redis_proxy",
    "envoy.filters.network.ratelimit",
    "envoy.filters.network.mongo_proxy",
    "envoy.filters.network.mysql_proxy",
    "envoy.filters.network.tcp_proxy",
    "envoy.filters.network.sni_dynamic_forward_proxy",
    "envoy.filters.udp_listener.udp_proxy",
    "envoy.resource_monitors.fixed_heap",
    "envoy.stat_sinks.statsd",
    "envoy.tracers.lightstep",
    "envoy.tracers.zipkin",
]

envoy_cc_test(
    name = "example_configs_test",
    srcs = [
        "example_configs_test.cc",
    ],
    data = [
        "example_configs_test_setup.sh",
        "//configs:example_configs",
    ],
    deps = [
        ":config_test_lib",
        "//source/common/filesystem:filesystem_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ] + select({
        "//bazel:windows_x86_64": [v for k, v in EXTENSIONS.items() if k in example_configs_extensions and k not in WINDOWS_SKIP_TARGETS],
        "//bazel:linux_ppc": [v for k, v in EXTENSIONS.items() if k in example_configs_extensions and k not in PPC_SKIP_TARGETS],
        "//conditions:default": [v for k, v in EXTENSIONS.items() if k in example_configs_extensions],
    }),
)

envoy_cc_test_library(
    name = "config_test_lib",
    srcs = ["config_test.cc"],
    hdrs = ["config_test.h"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/protobuf:utility_lib",
        "//source/server:configuration_lib",
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:listener_component_factory_mocks",
        "//test/mocks/server:worker_factory_mocks",
        "//test/mocks/server:worker_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "deprecated_configs_test",
    srcs = [
        "deprecated_configs_test.cc",
    ],
    deps = [
        ":config_test_lib",
        "//source/common/config:api_version_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
