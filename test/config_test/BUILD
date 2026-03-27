load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")
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

exports_files(["configs_test_setup.sh"])

label_flag(
    name = "configs",
    build_setting_default = "//configs",
)

label_flag(
    name = "test_lib",
    build_setting_default = ":config_test_lib",
)

envoy_cc_test_library(
    name = "test_runner_lib",
    srcs = [
        "configs_test.cc",
    ],
    deps = [
        ":test_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/extensions/config_subscription/rest:http_subscription_lib",
        "//source/extensions/filters/http/match_delegate:config",
        "//test/config:v2_link_hacks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    size = "large",
    data = [
        "configs_test_setup.sh",
        ":configs",
    ],
    env = {
        "DISABLE_TEST_MERGE": "true",
        "CONFIGS_TAR_PATH": "$(location :configs)",
        "GODEBUG": "cgocheck=0",
    },
    rbe_pool = "6gig",
    deps = [":test_runner_lib"],
)

envoy_cc_test_library(
    name = "config_test_lib",
    srcs = ["config_test.cc"],
    hdrs = ["config_test.h"],
    rbe_pool = "6gig",
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
        "//bazel:enable_http3_on_windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:enable_http3_on_linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//bazel:disable_http3_on_windows_x86_64": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS + WINDOWS_SKIP_TARGETS),
        "//bazel:disable_http3_on_linux_ppc": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS + PPC_SKIP_TARGETS),
        "//bazel:disable_http3": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
)

py_binary(
    name = "static_config_validation",
    srcs = ["static_config_validation.py"],
    args = (
        "--descriptor_path=$(location @envoy_api//:v3_proto_set)",
        "$(locations //configs:files) ",
    ),
    data = [
        "//configs:files",
        "@envoy_api//:v3_proto_set",
    ],
    deps = [requirement("envoy.base.utils")],
)
