licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

exports_files([
    "server.yaml",
    "server_unix_listener.yaml",
])

filegroup(
    name = "server_xds_files",
    srcs = [
        "server_xds.bootstrap.yaml",
        "server_xds.cds.yaml",
        "server_xds.eds.yaml",
        "server_xds.lds.yaml",
        "server_xds.rds.yaml",
    ],
)

filegroup(
    name = "server_config_files",
    srcs = [
        "server.yaml",
        "server_unix_listener.yaml",
    ],
)

filegroup(
    name = "google_com_proxy_port_0",
    srcs = ["google_com_proxy_port_0.v2.yaml"],
)
