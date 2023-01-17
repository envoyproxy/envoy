EXTENSIONS = {
    "envoy.transport_sockets.raw_buffer":               "//source/extensions/transport_sockets/raw_buffer:config",

    "envoy.filters.network.http_connection_manager":    "//source/extensions/filters/network/http_connection_manager:config",

    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",

    "envoy.filters.http.credentials":                   "//source/extensions/filters/http/credentials:config",
}

# These can be changed to ["//visibility:public"], for  downstream builds which
# need to directly reference Envoy extensions.
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config", "//:contrib_library", "//:examples_library"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library", "//:contrib_library", "//:examples_library"]
CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["//:contrib_library"]
