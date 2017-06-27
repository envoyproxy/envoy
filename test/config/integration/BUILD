licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

exports_files([
    "echo_server.json",
    "server.json",
    "server_grpc_json_transcoder.json",
    "server_http2.json",
    "server_http2_upstream.json",
    "server_proxy_proto.json",
    "server_ssl.json",
    "server_uds.json",
])

filegroup(
    name = "server_config_files",
    srcs = [
        "lightstep_access_token",
        "server.json",
    ],
)
