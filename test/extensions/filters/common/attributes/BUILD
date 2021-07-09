load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)


licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "id_test",
    srcs = ["id_test.cc"],
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
envoy_cc_test(
    name = "attributes_test",
    srcs = ["attributes_test.cc"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/common/attributes:attributes_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)