load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "id_test",
    srcs = ["id_test.cc"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/common/attributes:id_lib",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)