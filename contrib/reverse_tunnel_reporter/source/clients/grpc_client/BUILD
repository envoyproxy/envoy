load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "grpc_client_lib",
    srcs = [
        "client.cc",
        "factory.cc",
    ],
    hdrs = [
        "client.h",
        "factory.h",
    ],
    deps = [
        "//contrib/reverse_tunnel_reporter/source:reverse_tunnel_event_types",
        "//envoy/registry",
        "//source/common/common:logger_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client:pkg_cc_proto",
    ],
)
