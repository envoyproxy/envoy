# See bazel/README.md for details on how this system works.
EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                        "//source/extensions/access_loggers/file:config",
    "envoy.access_loggers.http_grpc":                   "//source/extensions/access_loggers/grpc:http_config",
    "envoy.access_loggers.tcp_grpc":                    "//source/extensions/access_loggers/grpc:tcp_config",
    "envoy.access_loggers.open_telemetry":              "//source/extensions/access_loggers/open_telemetry:config",
    "envoy.access_loggers.stream":                      "//source/extensions/access_loggers/stream:config",
    "envoy.access_loggers.wasm":                        "//source/extensions/access_loggers/wasm:config",

    #
    # Clusters
    #

    "envoy.clusters.aggregate":                         "//source/extensions/clusters/aggregate:cluster",
    "envoy.clusters.dynamic_forward_proxy":             "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.clusters.redis":                             "//source/extensions/clusters/redis:redis_cluster",

    #
    # Compression
    #

    "envoy.compression.gzip.compressor":                "//source/extensions/compression/gzip/compressor:config",
    "envoy.compression.gzip.decompressor":              "//source/extensions/compression/gzip/decompressor:config",
    "envoy.compression.brotli.compressor":              "//source/extensions/compression/brotli/compressor:config",
    "envoy.compression.brotli.decompressor":            "//source/extensions/compression/brotli/decompressor:config",

    #
    # gRPC Credentials Plugins
    #

    "envoy.grpc_credentials.file_based_metadata":       "//source/extensions/grpc_credentials/file_based_metadata:config",
    "envoy.grpc_credentials.aws_iam":                   "//source/extensions/grpc_credentials/aws_iam:config",

    #
    # WASM
    #

    "envoy.bootstrap.wasm":                             "//source/extensions/bootstrap/wasm:config",

    #
    # Health checkers
    #

    "envoy.health_checkers.redis":                      "//source/extensions/health_checkers/redis:config",

    #
    # Input Matchers
    #

    "envoy.matching.input_matchers.consistent_hashing":       "//source/extensions/matching/input_matchers/consistent_hashing:config",
    "envoy.matching.input_matchers.ip":                       "//source/extensions/matching/input_matchers/ip:config",

    #
    # Generic Inputs
    #

    "envoy.matching.common_inputs.environment_variable":       "//source/extensions/matching/common_inputs/environment_variable:config",

    #
    # HTTP filters
    #

    "envoy.filters.http.adaptive_concurrency":          "//source/extensions/filters/http/adaptive_concurrency:config",
    "envoy.filters.http.admission_control":             "//source/extensions/filters/http/admission_control:config",
    "envoy.filters.http.alternate_protocols_cache":     "//source/extensions/filters/http/alternate_protocols_cache:config",
    "envoy.filters.http.aws_lambda":                    "//source/extensions/filters/http/aws_lambda:config",
    "envoy.filters.http.aws_request_signing":           "//source/extensions/filters/http/aws_request_signing:config",
    "envoy.filters.http.bandwidth_limit":               "//source/extensions/filters/http/bandwidth_limit:config",
    "envoy.filters.http.buffer":                        "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.cache":                         "//source/extensions/filters/http/cache:config",
    "envoy.filters.http.cdn_loop":                      "//source/extensions/filters/http/cdn_loop:config",
    "envoy.filters.http.compressor":                    "//source/extensions/filters/http/compressor:config",
    "envoy.filters.http.cors":                          "//source/extensions/filters/http/cors:config",
    "envoy.filters.http.composite":                     "//source/extensions/filters/http/composite:config",
    "envoy.filters.http.csrf":                          "//source/extensions/filters/http/csrf:config",
    "envoy.filters.http.decompressor":                  "//source/extensions/filters/http/decompressor:config",
    "envoy.filters.http.dynamic_forward_proxy":         "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.dynamo":                        "//source/extensions/filters/http/dynamo:config",
    "envoy.filters.http.ext_authz":                     "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.ext_proc":                      "//source/extensions/filters/http/ext_proc:config",
    "envoy.filters.http.fault":                         "//source/extensions/filters/http/fault:config",
    "envoy.filters.http.grpc_http1_bridge":             "//source/extensions/filters/http/grpc_http1_bridge:config",
    "envoy.filters.http.grpc_http1_reverse_bridge":     "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
    "envoy.filters.http.grpc_json_transcoder":          "//source/extensions/filters/http/grpc_json_transcoder:config",
    "envoy.filters.http.grpc_stats":                    "//source/extensions/filters/http/grpc_stats:config",
    "envoy.filters.http.grpc_web":                      "//source/extensions/filters/http/grpc_web:config",
    "envoy.filters.http.header_to_metadata":            "//source/extensions/filters/http/header_to_metadata:config",
    "envoy.filters.http.health_check":                  "//source/extensions/filters/http/health_check:config",
    "envoy.filters.http.ip_tagging":                    "//source/extensions/filters/http/ip_tagging:config",
    "envoy.filters.http.jwt_authn":                     "//source/extensions/filters/http/jwt_authn:config",
    # Disabled by default
    "envoy.filters.http.kill_request":                  "//source/extensions/filters/http/kill_request:kill_request_config",
    "envoy.filters.http.local_ratelimit":               "//source/extensions/filters/http/local_ratelimit:config",
    "envoy.filters.http.lua":                           "//source/extensions/filters/http/lua:config",
    "envoy.filters.http.oauth2":                        "//source/extensions/filters/http/oauth2:config",
    "envoy.filters.http.on_demand":                     "//source/extensions/filters/http/on_demand:config",
    "envoy.filters.http.original_src":                  "//source/extensions/filters/http/original_src:config",
    "envoy.filters.http.ratelimit":                     "//source/extensions/filters/http/ratelimit:config",
    "envoy.filters.http.rbac":                          "//source/extensions/filters/http/rbac:config",
    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    "envoy.filters.http.set_metadata":                  "//source/extensions/filters/http/set_metadata:config",
    "envoy.filters.http.squash":                        "//source/extensions/filters/http/squash:config",
    "envoy.filters.http.tap":                           "//source/extensions/filters/http/tap:config",
    "envoy.filters.http.wasm":                          "//source/extensions/filters/http/wasm:config",

    #
    # Listener filters
    #

    "envoy.filters.listener.http_inspector":            "//source/extensions/filters/listener/http_inspector:config",
    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.original_dst":              "//source/extensions/filters/listener/original_dst:config",
    "envoy.filters.listener.original_src":              "//source/extensions/filters/listener/original_src:config",
    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol":            "//source/extensions/filters/listener/proxy_protocol:config",
    "envoy.filters.listener.tls_inspector":             "//source/extensions/filters/listener/tls_inspector:config",

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth":                      "//source/extensions/filters/network/client_ssl_auth:config",
    "envoy.filters.network.connection_limit":                     "//source/extensions/filters/network/connection_limit:config",
    "envoy.filters.network.direct_response":                      "//source/extensions/filters/network/direct_response:config",
    "envoy.filters.network.dubbo_proxy":                          "//source/extensions/filters/network/dubbo_proxy:config",
    "envoy.filters.network.echo":                                 "//source/extensions/filters/network/echo:config",
    "envoy.filters.network.ext_authz":                            "//source/extensions/filters/network/ext_authz:config",
    "envoy.filters.network.http_connection_manager":              "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.filters.network.kafka_broker":                         "//source/extensions/filters/network/kafka:kafka_broker_config_lib",
    "envoy.filters.network.local_ratelimit":                      "//source/extensions/filters/network/local_ratelimit:config",
    "envoy.filters.network.mongo_proxy":                          "//source/extensions/filters/network/mongo_proxy:config",
    "envoy.filters.network.mysql_proxy":                          "//source/extensions/filters/network/mysql_proxy:config",
    "envoy.filters.network.postgres_proxy":                       "//source/extensions/filters/network/postgres_proxy:config",
    "envoy.filters.network.ratelimit":                            "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.rbac":                                 "//source/extensions/filters/network/rbac:config",
    "envoy.filters.network.redis_proxy":                          "//source/extensions/filters/network/redis_proxy:config",
    "envoy.filters.network.rocketmq_proxy":                       "//source/extensions/filters/network/rocketmq_proxy:config",
    "envoy.filters.network.tcp_proxy":                            "//source/extensions/filters/network/tcp_proxy:config",
    "envoy.filters.network.thrift_proxy":                         "//source/extensions/filters/network/thrift_proxy:config",
    "envoy.filters.network.sni_cluster":                          "//source/extensions/filters/network/sni_cluster:config",
    "envoy.filters.network.sni_dynamic_forward_proxy":            "//source/extensions/filters/network/sni_dynamic_forward_proxy:config",
    "envoy.filters.network.wasm":                                 "//source/extensions/filters/network/wasm:config",
    "envoy.filters.network.zookeeper_proxy":                      "//source/extensions/filters/network/zookeeper_proxy:config",

    #
    # UDP filters
    #

    "envoy.filters.udp_listener.dns_filter":            "//source/extensions/filters/udp/dns_filter:config",
    "envoy.filters.udp_listener.udp_proxy":             "//source/extensions/filters/udp/udp_proxy:config",

    #
    # Resource monitors
    #

    "envoy.resource_monitors.fixed_heap":               "//source/extensions/resource_monitors/fixed_heap:config",
    "envoy.resource_monitors.injected_resource":        "//source/extensions/resource_monitors/injected_resource:config",

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd":                      "//source/extensions/stat_sinks/dog_statsd:config",
    "envoy.stat_sinks.graphite_statsd":                 "//source/extensions/stat_sinks/graphite_statsd:config",
    "envoy.stat_sinks.hystrix":                         "//source/extensions/stat_sinks/hystrix:config",
    "envoy.stat_sinks.metrics_service":                 "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.stat_sinks.statsd":                          "//source/extensions/stat_sinks/statsd:config",
    "envoy.stat_sinks.wasm":                            "//source/extensions/stat_sinks/wasm:config",

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
    "envoy.tracers.skywalking":                         "//source/extensions/tracers/skywalking:config",

    #
    # Transport sockets
    #

    "envoy.transport_sockets.alts":                     "//source/extensions/transport_sockets/alts:config",
    "envoy.transport_sockets.upstream_proxy_protocol":  "//source/extensions/transport_sockets/proxy_protocol:upstream_config",
    "envoy.transport_sockets.raw_buffer":               "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tap":                      "//source/extensions/transport_sockets/tap:config",
    "envoy.transport_sockets.starttls":                 "//source/extensions/transport_sockets/starttls:config",

    #
    # Retry host predicates
    #

    "envoy.retry_host_predicates.previous_hosts":       "//source/extensions/retry/host/previous_hosts:config",
    "envoy.retry_host_predicates.omit_canary_hosts":    "//source/extensions/retry/host/omit_canary_hosts:config",
    "envoy.retry_host_predicates.omit_host_metadata":   "//source/extensions/retry/host/omit_host_metadata:config",

    #
    # Retry priorities
    #

    "envoy.retry_priorities.previous_priorities":       "//source/extensions/retry/priority/previous_priorities:config",

    #
    # CacheFilter plugins
    #
    "envoy.cache.simple_http_cache":                    "//source/extensions/filters/http/cache/simple_http_cache:config",

    #
    # Internal redirect predicates
    #

    "envoy.internal_redirect_predicates.allow_listed_routes": "//source/extensions/internal_redirect/allow_listed_routes:config",
    "envoy.internal_redirect_predicates.previous_routes":     "//source/extensions/internal_redirect/previous_routes:config",
    "envoy.internal_redirect_predicates.safe_cross_scheme":   "//source/extensions/internal_redirect/safe_cross_scheme:config",

    #
    # Http Upstreams (excepting envoy.upstreams.http.generic which is hard-coded into the build so not registered here)
    #

    "envoy.upstreams.http.http":                        "//source/extensions/upstreams/http/http:config",
    "envoy.upstreams.http.tcp":                         "//source/extensions/upstreams/http/tcp:config",

    #
    # Watchdog actions
    #

    "envoy.watchdog.profile_action":                    "//source/extensions/watchdog/profile_action:config",

    #
    # WebAssembly runtimes
    #

    "envoy.wasm.runtime.null":                          "//source/extensions/wasm_runtime/null:config",
    "envoy.wasm.runtime.v8":                            "//source/extensions/wasm_runtime/v8:config",
    "envoy.wasm.runtime.wamr":                          "//source/extensions/wasm_runtime/wamr:config",
    "envoy.wasm.runtime.wavm":                          "//source/extensions/wasm_runtime/wavm:config",
    "envoy.wasm.runtime.wasmtime":                      "//source/extensions/wasm_runtime/wasmtime:config",

    #
    # Rate limit descriptors
    #

    "envoy.rate_limit_descriptors.expr":                "//source/extensions/rate_limit_descriptors/expr:config",

    #
    # IO socket
    #

    "envoy.io_socket.user_space":                       "//source/extensions/io_socket/user_space:config",

    #
    # TLS peer certification validators
    #

    "envoy.tls.cert_validator.spiffe":                  "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",

    #
    # HTTP header formatters
    #

    "envoy.http.stateful_header_formatters.preserve_case":       "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",

    #
    # Original IP detection
    #

    "envoy.http.original_ip_detection.custom_header":        "//source/extensions/http/original_ip_detection/custom_header:config",
    "envoy.http.original_ip_detection.xff":                  "//source/extensions/http/original_ip_detection/xff:config",

    #
    # Quic extensions
    #

    "envoy.quic.crypto_stream.server.quiche":           "//source/extensions/quic/crypto_stream:envoy_quic_default_crypto_server_stream",
    "envoy.quic.proof_source.filter_chain":             "//source/extensions/quic/proof_source:envoy_quic_default_proof_source",

    #
    # Formatter
    #

    "envoy.formatter.metadata":                         "//source/extensions/formatter/metadata:config",
    "envoy.formatter.req_without_query":                "//source/extensions/formatter/req_without_query:config",
}

# These can be changed to ["//visibility:public"], for  downstream builds which
# need to directly reference Envoy extensions.
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library"]
