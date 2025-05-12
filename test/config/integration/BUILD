load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "server.yaml",
    "server_multiple_addresses.yaml",
    "server_unix_listener.yaml",
])

filegroup(
    name = "server_xds_files",
    srcs = [
        "server_xds.bootstrap.udpa.yaml",
        "server_xds.bootstrap.yml",
        "server_xds.cds.with_unknown_field.yaml",
        "server_xds.cds.yaml",
        "server_xds.eds.ads_cluster.yaml",
        "server_xds.eds.with_unknown_field.yaml",
        "server_xds.eds.yaml",
        "server_xds.lds.typed_struct.yaml",
        "server_xds.lds.udpa.list_collection.yaml",
        "server_xds.lds.with_unknown_field.typed_struct.yaml",
        "server_xds.lds.with_unknown_field.yaml",
        "server_xds.lds.yaml",
        "server_xds.rds.with_unknown_field.yaml",
        "server_xds.rds.yaml",
    ],
)

filegroup(
    name = "server_config_files",
    srcs = [
        "empty.yaml",
        "server.yaml",
        "server_multiple_addresses.yaml",
        "server_unix_listener.yaml",
    ],
)

filegroup(
    name = "google_com_proxy_port_0",
    srcs = [
        "google_com_proxy_port_0.yaml",
    ],
)
