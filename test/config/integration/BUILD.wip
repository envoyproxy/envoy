package(default_visibility = ["//visibility:public"])

exports_files([
    "server.json",
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
