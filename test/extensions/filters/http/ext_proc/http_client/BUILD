load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "http_client_test",
    size = "small",
    srcs = ["http_client_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/ext_proc/http_client:http_client_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ext_proc_http_integration_test",
    srcs = ["ext_proc_http_integration_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    shard_count = 8,
    tags = [
        "cpu:3",
        "skip_on_windows",
    ],
    deps = [
        "//source/extensions/filters/http/ext_proc:config",
        "//test/common/http:common_lib",
        "//test/extensions/filters/http/ext_proc:utils_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)
