load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(["example_configs_test_setup.sh"])

envoy_cc_test(
    name = "example_configs_test",
    srcs = [
        "example_configs_test.cc",
    ],
    data = [
        "example_configs_test_setup.sh",
        "//configs:example_configs",
    ],
    tags = ["fails_on_windows"],
    deps = [
        ":config_test_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
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
        "//test/mocks/server:worker_factory_mocks",
        "//test/mocks/server:listener_component_factory_mocks",
        "//test/mocks/server:worker_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:simulated_time_system_lib",
    ] + select({
        "//bazel:windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
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
