load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "clients_lib",
    deps = [
        "//contrib/reverse_tunnel_reporter/source/clients/grpc_client:grpc_client_lib",
    ],
)
