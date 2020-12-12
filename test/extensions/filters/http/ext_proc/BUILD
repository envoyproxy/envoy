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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.ext_proc",
    deps = [
        "//source/extensions/filters/http/ext_proc:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.http.ext_proc",
    deps = [
        "//source/extensions/filters/http/ext_proc",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "client_test",
    srcs = ["client_test.cc"],
    extension_name = "envoy.filters.http.ext_proc",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/ext_proc:client_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
