load("//bazel:envoy_build_system.bzl", "envoy_cc_fuzz_test", "envoy_cc_test", "envoy_cc_test_library", "envoy_package", "envoy_proto_library")
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "cluster_manager_test",
    srcs = ["cluster_manager_test.cc"],
    deps = [
        "//envoy/upstream:resource_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/api:api_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//source/server/config_validation:cluster_manager_lib",
        "//source/server/config_validation:dns_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:options_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

filegroup(
    name = "server_test_data",
    srcs = glob(["test_data/**"]),
)

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        ":server_test_data",
        "//configs:example_configs",
        "//test/config_test:example_configs_test_setup.sh",
    ],
    env = {"EXAMPLE_CONFIGS_TAR_PATH": "envoy/configs/example_configs.tar"},
    deps = [
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/listener/original_dst:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/listener_managers/validation_listener_manager:validation_listener_manager_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:options_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dispatcher_test",
    srcs = ["dispatcher_test.cc"],
    deps = [
        "//source/common/event:libevent_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/server/config_validation:api_lib",
        "//source/server/config_validation:dns_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "config_fuzz_test",
    size = "large",
    srcs = ["config_fuzz_test.cc"],
    corpus = "//test/server:server_fuzz_test_corpus",
    deps = [
        "//source/common/common:thread_lib",
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:environment_lib",
    ] + select({
        "//bazel:windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//bazel:gcc_build": [],
        "//conditions:default": envoy_all_extensions(),
    }),
)

envoy_proto_library(
    name = "xds_fuzz_proto",
    srcs = ["xds_fuzz.proto"],
)

envoy_cc_test_library(
    name = "xds_verifier_lib",
    srcs = ["xds_verifier.cc"],
    hdrs = ["xds_verifier.h"],
    deps = [
        ":xds_fuzz_proto_cc_proto",
        "//envoy/common:exception_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xds_verifier_test",
    srcs = ["xds_verifier_test.cc"],
    deps = [
        ":xds_verifier_lib",
        "//test/config:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "xds_fuzz_lib",
    srcs = ["xds_fuzz.cc"],
    hdrs = ["xds_fuzz.h"],
    deps = [
        ":xds_fuzz_proto_cc_proto",
        ":xds_verifier_lib",
        "//source/common/common:matchers_lib",
        "//test/fuzz:utility_lib",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "xds_fuzz_test",
    size = "large",
    srcs = ["xds_fuzz_test.cc"],
    corpus = "xds_corpus",
    deps = [
        ":xds_fuzz_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
