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
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    extension_names = ["envoy.filters.http.ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/http/ratelimit:ratelimit_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_mocks",
        "//test/extensions/filters/common/ratelimit:ratelimit_utils",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/ratelimit:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ratelimit_integration_test",
    size = "large",
    srcs = ["ratelimit_integration_test.cc"],
    extension_names = ["envoy.filters.http.ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/ratelimit:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_utils",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ratelimit_headers_test",
    srcs = ["ratelimit_headers_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/ratelimit:ratelimit_headers_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_utils",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)
