# See bazel/README.md for details on how this system works.
EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                        "//source/extensions/access_loggers/file:config",
    "envoy.access_loggers.http_grpc":                   "//source/extensions/access_loggers/grpc:http_config",
    "envoy.access_loggers.tcp_grpc":                    "//source/extensions/access_loggers/grpc:tcp_config",

    #
    # Clusters
    #
    "envoy.clusters.dynamic_forward_proxy":             "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.clusters.redis":                             "//source/extensions/clusters/redis:redis_cluster",

    #
    # gRPC Credentials Plugins
    #

    "envoy.grpc_credentials.file_based_metadata":       "//source/extensions/grpc_credentials/file_based_metadata:config",
    "envoy.grpc_credentials.aws_iam":                   "//source/extensions/grpc_credentials/aws_iam:config",

    #
    # Health checkers
    #

    "envoy.health_checkers.redis":                      "//source/extensions/health_checkers/redis:config",

    #
    # HTTP filters
    #

    "envoy.filters.http.adaptive_concurrency":          "//source/extensions/filters/http/adaptive_concurrency:config",
    "envoy.filters.http.buffer":                        "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.cors":                          "//source/extensions/filters/http/cors:config",
    "envoy.filters.http.csrf":                          "//source/extensions/filters/http/csrf:config",
    "envoy.filters.http.dynamic_forward_proxy":         "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.dynamo":                        "//source/extensions/filters/http/dynamo:config",
    "envoy.filters.http.ext_authz":                     "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.fault":                         "//source/extensions/filters/http/fault:config",
    "envoy.filters.http.grpc_http1_bridge":             "//source/extensions/filters/http/grpc_http1_bridge:config",
    "envoy.filters.http.grpc_http1_reverse_bridge":     "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
    "envoy.filters.http.grpc_json_transcoder":          "//source/extensions/filters/http/grpc_json_transcoder:config",
    "envoy.filters.http.grpc_stats":                    "//source/extensions/filters/http/grpc_stats:config",
    "envoy.filters.http.grpc_web":                      "//source/extensions/filters/http/grpc_web:config",
    "envoy.filters.http.gzip":                          "//source/extensions/filters/http/gzip:config",
    "envoy.filters.http.header_to_metadata":            "//source/extensions/filters/http/header_to_metadata:config",
    "envoy.filters.http.health_check":                  "//source/extensions/filters/http/health_check:config",
    "envoy.filters.http.ip_tagging":                    "//source/extensions/filters/http/ip_tagging:config",
    "envoy.filters.http.jwt_authn":                     "//source/extensions/filters/http/jwt_authn:config",
    "envoy.filters.http.lua":                           "//source/extensions/filters/http/lua:config",
    "envoy.filters.http.original_src":                  "//source/extensions/filters/http/original_src:config",
    "envoy.filters.http.ratelimit":                     "//source/extensions/filters/http/ratelimit:config",
    "envoy.filters.http.rbac":                          "//source/extensions/filters/http/rbac:config",
    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    "envoy.filters.http.squash":                        "//source/extensions/filters/http/squash:config",
    "envoy.filters.http.tap":                           "//source/extensions/filters/http/tap:config",

    #
    # Listener filters
    #

    "envoy.filters.listener.http_inspector":            "//source/extensions/filters/listener/http_inspector:config",
    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.original_dst":              "//source/extensions/filters/listener/original_dst:config",
    "envoy.filters.listener.original_src":               "//source/extensions/filters/listener/original_src:config",
    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol":            "//source/extensions/filters/listener/proxy_protocol:config",
    "envoy.filters.listener.tls_inspector":             "//source/extensions/filters/listener/tls_inspector:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":            "//source/extensions/filters/network/client_ssl_auth:config",
    "envoy.filters.network.dubbo_proxy":                "//source/extensions/filters/network/dubbo_proxy:config",
    "envoy.filters.network.echo":                       "//source/extensions/filters/network/echo:config",
    "envoy.filters.network.ext_authz":                  "//source/extensions/filters/network/ext_authz:config",
    "envoy.filters.network.http_connection_manager":    "//source/extensions/filters/network/http_connection_manager:config",
    # NOTE: Kafka filter does not have a proper filter implemented right now. We are referencing to
    #       codec implementation that is going to be used by the filter.
    "envoy.filters.network.kafka":                      "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
    "envoy.filters.network.mongo_proxy":                "//source/extensions/filters/network/mongo_proxy:config",
    "envoy.filters.network.mysql_proxy":                "//source/extensions/filters/network/mysql_proxy:config",
    "envoy.filters.network.ratelimit":                  "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.rbac":                       "//source/extensions/filters/network/rbac:config",
    "envoy.filters.network.redis_proxy":                "//source/extensions/filters/network/redis_proxy:config",
    "envoy.filters.network.tcp_proxy":                  "//source/extensions/filters/network/tcp_proxy:config",
    "envoy.filters.network.thrift_proxy":               "//source/extensions/filters/network/thrift_proxy:config",
    "envoy.filters.network.sni_cluster":                "//source/extensions/filters/network/sni_cluster:config",
    "envoy.filters.network.zookeeper_proxy":            "//source/extensions/filters/network/zookeeper_proxy:config",

    #
    # Resource monitors
    #

    "envoy.resource_monitors.fixed_heap":               "//source/extensions/resource_monitors/fixed_heap:config",
    "envoy.resource_monitors.injected_resource":        "//source/extensions/resource_monitors/injected_resource:config",

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd":                      "//source/extensions/stat_sinks/dog_statsd:config",
    "envoy.stat_sinks.hystrix":                         "//source/extensions/stat_sinks/hystrix:config",
    "envoy.stat_sinks.metrics_service":                 "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.stat_sinks.statsd":                          "//source/extensions/stat_sinks/statsd:config",

    #
    # Thrift filters
    #

    "envoy.filters.thrift.router":                      "//source/extensions/filters/network/thrift_proxy/router:config",
    "envoy.filters.thrift.ratelimit":                   "//source/extensions/filters/network/thrift_proxy/filters/ratelimit:config",

    #
    # Tracers
    #

    "envoy.tracers.dynamic_ot":                         "//source/extensions/tracers/dynamic_ot:config",
    "envoy.tracers.lightstep":                          "//source/extensions/tracers/lightstep:config",
    "envoy.tracers.datadog":                            "//source/extensions/tracers/datadog:config",
    "envoy.tracers.zipkin":                             "//source/extensions/tracers/zipkin:config",
    "envoy.tracers.opencensus":                         "//source/extensions/tracers/opencensus:config",
    "envoy.tracers.xray":                               "//source/extensions/tracers/xray:config",

    #
    # Transport sockets
    #

    "envoy.transport_sockets.alts":                     "//source/extensions/transport_sockets/alts:config",
    "envoy.transport_sockets.tap":                      "//source/extensions/transport_sockets/tap:config",
    "envoy.transport_sockets.tls":                      "//source/extensions/transport_sockets/tls:config",

    # Retry host predicates
    "envoy.retry_host_predicates.previous_hosts":          "//source/extensions/retry/host/previous_hosts:config",
    "envoy.retry_host_predicates.omit_canary_hosts":            "//source/extensions/retry/host/omit_canary_hosts:config",
    
    # Retry priorities
    "envoy.retry_priorities.previous_priorities":       "//source/extensions/retry/priority/previous_priorities:config",
}

WINDOWS_EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                        "//source/extensions/access_loggers/file:config",
    #"envoy.access_loggers.http_grpc":                   "//source/extensions/access_loggers/grpc:http_config",

    #
    # gRPC Credentials Plugins
    #

    #"envoy.grpc_credentials.file_based_metadata":      "//source/extensions/grpc_credentials/file_based_metadata:config",

    #
    # Health checkers
    #

    #"envoy.health_checkers.redis":                      "//source/extensions/health_checkers/redis:config",

    #
    # HTTP filters
    #

    #"envoy.filters.http.buffer":                        "//source/extensions/filters/http/buffer:config",
    #"envoy.filters.http.cors":                          "//source/extensions/filters/http/cors:config",
    #"envoy.filters.http.csrf":                          "//source/extensions/filters/http/csrf:config",
    #"envoy.filters.http.dynamo":                        "//source/extensions/filters/http/dynamo:config",
    #"envoy.filters.http.ext_authz":                     "//source/extensions/filters/http/ext_authz:config",
    #"envoy.filters.http.fault":                         "//source/extensions/filters/http/fault:config",
    #"envoy.filters.http.grpc_http1_bridge":             "//source/extensions/filters/http/grpc_http1_bridge:config",
    #"envoy.filters.http.grpc_json_transcoder":          "//source/extensions/filters/http/grpc_json_transcoder:config",
    #"envoy.filters.http.grpc_web":                      "//source/extensions/filters/http/grpc_web:config",
    #"envoy.filters.http.gzip":                          "//source/extensions/filters/http/gzip:config",
    #"envoy.filters.http.health_check":                  "//source/extensions/filters/http/health_check:config",
    #"envoy.filters.http.ip_tagging":                    "//source/extensions/filters/http/ip_tagging:config",
    #"envoy.filters.http.lua":                           "//source/extensions/filters/http/lua:config",
    #"envoy.filters.http.ratelimit":                     "//source/extensions/filters/http/ratelimit:config",
    #"envoy.filters.http.rbac":                          "//source/extensions/filters/http/rbac:config",
    #"envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    #"envoy.filters.http.squash":                        "//source/extensions/filters/http/squash:config",

    #
    # Listener filters
    #

    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol":            "//source/extensions/filters/listener/proxy_protocol:config",

    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    #"envoy.filters.listener.original_dst":              "//source/extensions/filters/listener/original_dst:config",

    "envoy.filters.listener.tls_inspector":             "//source/extensions/filters/listener/tls_inspector:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":            "//source/extensions/filters/network/client_ssl_auth:config",
    #"envoy.filters.network.echo":                       "//source/extensions/filters/network/echo:config",
    #"envoy.filters.network.ext_authz":                  "//source/extensions/filters/network/ext_authz:config",
    #"envoy.filters.network.http_connection_manager":    "//source/extensions/filters/network/http_connection_manager:config",
    #"envoy.filters.network.mongo_proxy":                "//source/extensions/filters/network/mongo_proxy:config",
    #"envoy.filters.network.mysql_proxy":                "//source/extensions/filters/network/mysql_proxy:config",
    #"envoy.filters.network.redis_proxy":                "//source/extensions/filters/network/redis_proxy:config",
    #"envoy.filters.network.ratelimit":                  "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.tcp_proxy":                  "//source/extensions/filters/network/tcp_proxy:config",
    #"envoy.filters.network.thrift_proxy":               "//source/extensions/filters/network/thrift_proxy:config",
    #"envoy.filters.network.sni_cluster":                "//source/extensions/filters/network/sni_cluster:config",
    #"envoy.filters.network.zookeeper_proxy":            "//source/extensions/filters/network/zookeeper_proxy:config",

    #
    # Stat sinks
    #

    #"envoy.stat_sinks.dog_statsd":                      "//source/extensions/stat_sinks/dog_statsd:config",
    #"envoy.stat_sinks.metrics_service":                 "//source/extensions/stat_sinks/metrics_service:config",
    #"envoy.stat_sinks.statsd":                          "//source/extensions/stat_sinks/statsd:config",

    #
    # Tracers
    #

    #"envoy.tracers.dynamic_ot":                         "//source/extensions/tracers/dynamic_ot:config",
    #"envoy.tracers.lightstep":                          "//source/extensions/tracers/lightstep:config",
    #"envoy.tracers.zipkin":                             "//source/extensions/tracers/zipkin:config",

    #
    # Transport sockets
    #

    #"envoy.transport_sockets.tap":                      "//source/extensions/transport_sockets/tap:config",
}
