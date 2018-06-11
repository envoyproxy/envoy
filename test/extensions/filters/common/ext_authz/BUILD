licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "check_request_utils_test",
    srcs = ["check_request_utils_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/common/ext_authz:check_request_utils_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/request_info:request_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ext_authz_grpc_impl_test",
    srcs = ["ext_authz_grpc_impl_test.cc"],
    deps = [
        "//source/extensions/filters/common/ext_authz:ext_authz_grpc_lib",
        "//test/extensions/filters/common/ext_authz:ext_authz_test_common",
    ],
)

envoy_cc_test(
    name = "ext_authz_http_impl_test",
    srcs = ["ext_authz_http_impl_test.cc"],
    deps = [
        "//source/extensions/filters/common/ext_authz:ext_authz_http_lib",
        "//test/extensions/filters/common/ext_authz:ext_authz_test_common",
    ],
)

envoy_cc_mock(
    name = "ext_authz_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/filters/common/ext_authz:ext_authz_interface",
    ],
)

envoy_cc_mock(
    name = "ext_authz_test_common",
    srcs = ["test_common.cc"],
    hdrs = ["test_common.h"],
    deps = [
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/common/ext_authz:ext_authz_grpc_lib",
        "//test/extensions/filters/common/ext_authz:ext_authz_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)
