load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ext_authz_interface",
    hdrs = ["ext_authz.h"],
    deps = [
        "//include/envoy/http:codes_interface_no_include_prefix",
        "//include/envoy/stream_info:stream_info_interface_no_include_prefix",
        "//source/common/http:headers_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ext_authz_grpc_lib",
    srcs = ["ext_authz_grpc_impl.cc"],
    hdrs = ["ext_authz_grpc_impl.h"],
    deps = [
        ":check_request_utils_lib",
        ":ext_authz_interface",
        "//include/envoy/grpc:async_client_interface_no_include_prefix",
        "//include/envoy/grpc:async_client_manager_interface_no_include_prefix",
        "//include/envoy/http:filter_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
        "//include/envoy/http:protocol_interface_no_include_prefix",
        "//include/envoy/network:address_interface_no_include_prefix",
        "//include/envoy/network:connection_interface_no_include_prefix",
        "//include/envoy/network:filter_interface_no_include_prefix",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_authz/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ext_authz_http_lib",
    srcs = ["ext_authz_http_impl.cc"],
    hdrs = ["ext_authz_http_impl.h"],
    deps = [
        ":check_request_utils_lib",
        ":ext_authz_interface",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:codes_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_authz/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "check_request_utils_lib",
    srcs = ["check_request_utils.cc"],
    hdrs = ["check_request_utils.h"],
    deps = [
        ":ext_authz_interface",
        "//include/envoy/grpc:async_client_interface_no_include_prefix",
        "//include/envoy/grpc:async_client_manager_interface_no_include_prefix",
        "//include/envoy/http:filter_interface_no_include_prefix",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
    ],
)
