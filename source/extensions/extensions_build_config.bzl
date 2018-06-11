# See bazel/README.md for details on how this system works.
EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                        "//source/extensions/access_loggers/file:config",
    "envoy.access_loggers.http_grpc":                   "//source/extensions/access_loggers/http_grpc:config",

    #
    # gRPC Credentials Plugins
    #

    "envoy.grpc_credentials.file_based_metadata":      "//source/extensions/grpc_credentials/file_based_metadata:config",

    #
    # Health checkers
    #

    "envoy.health_checkers.redis":                      "//source/extensions/health_checkers/redis:config",

    #
    # HTTP filters
    #

    "envoy.filters.http.buffer":                        "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.cors":                          "//source/extensions/filters/http/cors:config",
    "envoy.filters.http.dynamo":                        "//source/extensions/filters/http/dynamo:config",
    "envoy.filters.http.ext_authz":                     "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.fault":                         "//source/extensions/filters/http/fault:config",
    "envoy.filters.http.grpc_http1_bridge":             "//source/extensions/filters/http/grpc_http1_bridge:config",
    "envoy.filters.http.grpc_json_transcoder":          "//source/extensions/filters/http/grpc_json_transcoder:config",
    "envoy.filters.http.grpc_web":                      "//source/extensions/filters/http/grpc_web:config",
    "envoy.filters.http.gzip":                          "//source/extensions/filters/http/gzip:config",
    "envoy.filters.http.health_check":                  "//source/extensions/filters/http/health_check:config",
    "envoy.filters.http.ip_tagging":                    "//source/extensions/filters/http/ip_tagging:config",
    "envoy.filters.http.lua":                           "//source/extensions/filters/http/lua:config",
    "envoy.filters.http.ratelimit":                     "//source/extensions/filters/http/ratelimit:config",
    "envoy.filters.http.rbac":                          "//source/extensions/filters/http/rbac:config",
    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    "envoy.filters.http.squash":                        "//source/extensions/filters/http/squash:config",

    #
    # Listener filters
    #

    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol":            "//source/extensions/filters/listener/proxy_protocol:config",

    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.original_dst":              "//source/extensions/filters/listener/original_dst:config",

    "envoy.filters.listener.tls_inspector":             "//source/extensions/filters/listener/tls_inspector:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":            "//source/extensions/filters/network/client_ssl_auth:config",
    "envoy.filters.network.echo":                       "//source/extensions/filters/network/echo:config",
    "envoy.filters.network.ext_authz":                  "//source/extensions/filters/network/ext_authz:config",
    "envoy.filters.network.http_connection_manager":    "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.filters.network.mongo_proxy":                "//source/extensions/filters/network/mongo_proxy:config",
    "envoy.filters.network.redis_proxy":                "//source/extensions/filters/network/redis_proxy:config",
    "envoy.filters.network.ratelimit":                  "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.tcp_proxy":                  "//source/extensions/filters/network/tcp_proxy:config",
    # TODO(zuercher): switch to config target once a filter exists
    "envoy.filters.network.thrift_proxy":               "//source/extensions/filters/network/thrift_proxy:transport_lib",

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd":                      "//source/extensions/stat_sinks/dog_statsd:config",
    "envoy.stat_sinks.metrics_service":                 "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.stat_sinks.statsd":                          "//source/extensions/stat_sinks/statsd:config",

    #
    # Tracers
    #

    "envoy.tracers.dynamic_ot":                         "//source/extensions/tracers/dynamic_ot:config",
    "envoy.tracers.lightstep":                          "//source/extensions/tracers/lightstep:config",
    "envoy.tracers.zipkin":                             "//source/extensions/tracers/zipkin:config",

    #
    # Transport sockets
    #

    "envoy.transport_sockets.capture":                  "//source/extensions/transport_sockets/capture:config",
}
