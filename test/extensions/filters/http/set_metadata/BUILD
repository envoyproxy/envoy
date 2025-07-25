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
    name = "set_metadata_filter_test",
    srcs = ["set_metadata_filter_test.cc"],
    extension_names = ["envoy.filters.http.set_metadata"],
    deps = [
        "//source/extensions/filters/http/set_metadata:set_metadata_filter_lib",
        "//source/server:generic_factory_context_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "set_metadata_integration_test",
    srcs = ["set_metadata_integration_test.cc"],
    extension_names = ["envoy.filters.http.set_metadata"],
    deps = [
        "//source/extensions/filters/http/set_metadata:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/set_metadata/v3:pkg_cc_proto",
    ],
)
