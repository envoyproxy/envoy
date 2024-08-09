load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "tls_session_cache_lib",
    srcs = [
        "session_cache_impl.cc",
    ],
    hdrs = [
        "session_cache_impl.h",
    ],
    copts = [
        "-Wno-comments",
        "-Wno-unused-parameter",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":tls_session_cache_client_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/tls_session_cache/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tls_session_cache_client_interface",
    hdrs = ["session_cache.h"],
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/tls_session_cache/v3:pkg_cc_proto",
    ],
)
