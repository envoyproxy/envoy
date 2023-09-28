load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel:repositories.bzl", "NO_HTTP3_SKIP_TARGETS", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(["example_configs_test_setup.sh"])

envoy_cc_test_library(
    name = "example_configs_test_lib",
    srcs = [
        "example_configs_test.cc",
    ],
    deps = [
        ":config_test_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/extensions/config_subscription/rest:http_subscription_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "example_configs_test",
    size = "large",
    data = [
        "example_configs_test_setup.sh",
        "//configs:example_configs",
    ],
    env = {
        "EXAMPLE_CONFIGS_TAR_PATH": "envoy/configs/example_configs.tar",
        "GODEBUG": "cgocheck=0",
    },
    deps = [
        ":example_configs_test_lib",
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
        "//test/mocks/server:listener_component_factory_mocks",
        "//test/mocks/server:worker_factory_mocks",
        "//test/mocks/server:worker_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ] + select({
        "//bazel:windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//bazel:disable_http3": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
)
