# See bazel/README.md for details on how this system works.
EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                "//source/extensions/access_loggers/file:config",
    "envoy.access_loggers.http_grpc":           "//source/extensions/access_loggers/http_grpc:config",

    #
    # HTTP filters
    #

    "envoy.filters.http.dynamo":                "//source/extensions/filters/http/dynamo:config",
    "envoy.filters.http.ext_authz":             "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.lua":                   "//source/extensions/filters/http/lua:config",
    "envoy.filters.http.ratelimit":             "//source/extensions/filters/http/ratelimit:config",

    #
    # Listener filters
    #

    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol":    "//source/extensions/filters/listener/proxy_protocol:config",

    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.original_dst":      "//source/extensions/filters/listener/original_dst:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":    "//source/extensions/filters/network/client_ssl_auth:config",
    "envoy.filters.network.echo":               "//source/extensions/filters/network/echo:config",
    "envoy.filters.network.ext_authz":          "//source/extensions/filters/network/ext_authz:config",
    "envoy.filters.network.mongo_proxy":        "//source/extensions/filters/network/mongo_proxy:config",
    "envoy.filters.network.redis_proxy":        "//source/extensions/filters/network/redis_proxy:config",
    "envoy.filters.network.ratelimit":          "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.tcp_proxy":          "//source/extensions/filters/network/tcp_proxy:config",

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd":              "//source/extensions/stat_sinks/dog_statsd:config",
    "envoy.stat_sinks.metrics_service":         "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.stat_sinks.statsd":                  "//source/extensions/stat_sinks/statsd:config",

    #
    # Tracers
    #

    "envoy.tracers.dynamic_ot":                 "//source/extensions/tracers/dynamic_ot:config",
    "envoy.tracers.lightstep":                  "//source/extensions/tracers/lightstep:config",
    "envoy.tracers.zipkin":                     "//source/extensions/tracers/zipkin:config",
}
