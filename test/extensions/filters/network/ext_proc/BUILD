load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/network/ext_proc:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "matching_utils_test",
    srcs = ["matching_utils_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/network/ext_proc:matching_utils_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ext_proc_test",
    srcs = ["ext_proc_test.cc"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/network/ext_proc:ext_proc_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ext_proc_integration_test",
    srcs = ["ext_proc_integration_test.cc"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/extensions/filters/network/ext_proc:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/config:utility_lib",
        "//test/integration:base_integration_test_lib",
        "//test/integration:fake_upstream_lib",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/service/network_ext_proc/v3:pkg_cc_proto",
    ],
)
