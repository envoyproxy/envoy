EXTENSIONS = {
    "envoy.clusters.dynamic_forward_proxy":             "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.filters.http.dynamic_forward_proxy":         "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    "envoy.filters.network.http_connection_manager":    "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.transport_sockets.tls":                      "//source/extensions/transport_sockets/tls:config",
}
WINDOWS_EXTENSIONS = {}
