# See bazel/README.md for details on how this system works.
EXTENSIONS = {
    #
    # Access loggers
    #

    "envoy.access_loggers.file":                        "//source/extensions/access_loggers/file:config",
    "envoy.access_loggers.extension_filters.cel":       "//source/extensions/access_loggers/filters/cel:config",
    "envoy.access_loggers.extension_filters.process_ratelimit":       "//source/extensions/access_loggers/filters/process_ratelimit:config",
    "envoy.access_loggers.fluentd"  :                   "//source/extensions/access_loggers/fluentd:config",
    "envoy.access_loggers.http_grpc":                   "//source/extensions/access_loggers/grpc:http_config",
    "envoy.access_loggers.tcp_grpc":                    "//source/extensions/access_loggers/grpc:tcp_config",
    "envoy.access_loggers.open_telemetry":              "//source/extensions/access_loggers/open_telemetry:config",
    "envoy.access_loggers.stats":                       "//source/extensions/access_loggers/stats:config",
    "envoy.access_loggers.stdout":                      "//source/extensions/access_loggers/stream:config",
    "envoy.access_loggers.stderr":                      "//source/extensions/access_loggers/stream:config",
    "envoy.access_loggers.wasm":                        "//source/extensions/access_loggers/wasm:config",

    #
    # Clusters
    #

    "envoy.clusters.aggregate":                         "//source/extensions/clusters/aggregate:cluster",
    "envoy.clusters.dns":                               "//source/extensions/clusters/dns:dns_cluster_lib",
    "envoy.clusters.dynamic_forward_proxy":             "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.clusters.eds":                               "//source/extensions/clusters/eds:eds_lib",
    "envoy.clusters.redis":                             "//source/extensions/clusters/redis:redis_cluster",
    "envoy.clusters.static":                            "//source/extensions/clusters/static:static_cluster_lib",
    "envoy.clusters.strict_dns":                        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
    "envoy.clusters.original_dst":                      "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
    "envoy.clusters.logical_dns":                       "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
    "envoy.clusters.reverse_connection":                "//source/extensions/clusters/reverse_connection:reverse_connection_lib",

    #
    # Compression
    #

    "envoy.compression.gzip.compressor":                "//source/extensions/compression/gzip/compressor:config",
    "envoy.compression.gzip.decompressor":              "//source/extensions/compression/gzip/decompressor:config",
    "envoy.compression.brotli.compressor":              "//source/extensions/compression/brotli/compressor:config",
    "envoy.compression.brotli.decompressor":            "//source/extensions/compression/brotli/decompressor:config",
    "envoy.compression.zstd.compressor":                "//source/extensions/compression/zstd/compressor:config",
    "envoy.compression.zstd.decompressor":              "//source/extensions/compression/zstd/decompressor:config",

    #
    # Config validators
    #

    "envoy.config.validators.minimum_clusters_validator":     "//source/extensions/config/validators/minimum_clusters:config",

    #
    # gRPC Credentials Plugins
    #

    "envoy.grpc_credentials.file_based_metadata":       "//source/extensions/grpc_credentials/file_based_metadata:config",

    #
    # WASM
    #

    "envoy.bootstrap.wasm":                             "//source/extensions/bootstrap/wasm:config",

    #
    # Reverse Connection
    #

    "envoy.bootstrap.reverse_tunnel.downstream_socket_interface": "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
    "envoy.bootstrap.reverse_tunnel.upstream_socket_interface": "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",

    #
    # Health checkers
    #

    "envoy.health_checkers.redis":                      "//source/extensions/health_checkers/redis:config",
    "envoy.health_checkers.thrift":                     "//source/extensions/health_checkers/thrift:config",
    "envoy.health_checkers.tcp":                        "//source/extensions/health_checkers/tcp:health_checker_lib",
    "envoy.health_checkers.http":                       "//source/extensions/health_checkers/http:health_checker_lib",
    "envoy.health_checkers.grpc":                       "//source/extensions/health_checkers/grpc:health_checker_lib",

    #
    # Health check event sinks
    #

    "envoy.health_check.event_sinks.file":              "//source/extensions/health_check/event_sinks/file:file_sink_lib",

    #
    # Input Matchers
    #

    "envoy.matching.matchers.consistent_hashing":       "//source/extensions/matching/input_matchers/consistent_hashing:config",
    "envoy.matching.matchers.ip":                       "//source/extensions/matching/input_matchers/ip:config",
    "envoy.matching.matchers.runtime_fraction":         "//source/extensions/matching/input_matchers/runtime_fraction:config",
    "envoy.matching.matchers.cel_matcher":              "//source/extensions/matching/input_matchers/cel_matcher:config",
    "envoy.matching.matchers.metadata_matcher":         "//source/extensions/matching/input_matchers/metadata:config",

    #
    # Network Matchers
    #

    "envoy.matching.inputs.application_protocol":       "//source/extensions/matching/network/application_protocol:config",
    # Ideally these would be split up. We'll do so if anyone cares.
    "envoy.matching.inputs.destination_ip":             "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.destination_port":           "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.source_ip":                  "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.source_port":                "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.direct_source_ip":           "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.source_type":                "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.server_name":                "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.network_namespace":          "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.transport_protocol":         "//source/extensions/matching/network/common:inputs_lib",
    "envoy.matching.inputs.filter_state":               "//source/extensions/matching/network/common:inputs_lib",

    #
    # Generic Inputs
    #

    "envoy.matching.common_inputs.environment_variable":       "//source/extensions/matching/common_inputs/environment_variable:config",

    #
    # CEL Matching Input
    #
    "envoy.matching.inputs.cel_data_input":             "//source/extensions/matching/http/cel_input:cel_input_lib",

    #
    # Dynamic Metadata Matching Input
    #
    "envoy.matching.inputs.dynamic_metadata":           "//source/extensions/matching/http/metadata_input:metadata_input_lib",

    #
    # Transport Socket Matching Inputs
    #
    "envoy.matching.inputs.endpoint_metadata":     "//source/extensions/matching/common_inputs/transport_socket:config",
    "envoy.matching.inputs.locality_metadata":     "//source/extensions/matching/common_inputs/transport_socket:config",
    "envoy.matching.inputs.transport_socket_filter_state": "//source/extensions/matching/common_inputs/transport_socket:config",

    #
    # Matching actions
    #

    "envoy.matching.actions.format_string":             "//source/extensions/matching/actions/format_string:config",
    "envoy.matching.action.transport_socket.name":      "//source/extensions/matching/common_inputs/transport_socket:config",

    #
    # StringMatchers
    #
    "envoy.string_matcher.lua":                         "//source/extensions/string_matcher/lua:config",

    #
    # HTTP filters
    #

    "envoy.filters.http.adaptive_concurrency":          "//source/extensions/filters/http/adaptive_concurrency:config",
    "envoy.filters.http.admission_control":             "//source/extensions/filters/http/admission_control:config",
    "envoy.filters.http.alternate_protocols_cache":     "//source/extensions/filters/http/alternate_protocols_cache:config",
    "envoy.filters.http.api_key_auth":                  "//source/extensions/filters/http/api_key_auth:config",
    "envoy.filters.http.aws_lambda":                    "//source/extensions/filters/http/aws_lambda:config",
    "envoy.filters.http.aws_request_signing":           "//source/extensions/filters/http/aws_request_signing:config",
    "envoy.filters.http.bandwidth_limit":               "//source/extensions/filters/http/bandwidth_limit:config",
    "envoy.filters.http.basic_auth":                    "//source/extensions/filters/http/basic_auth:config",
    "envoy.filters.http.buffer":                        "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.cache":                         "//source/extensions/filters/http/cache:config",
    "envoy.filters.http.cache_v2":                      "//source/extensions/filters/http/cache_v2:config",
    "envoy.filters.http.cdn_loop":                      "//source/extensions/filters/http/cdn_loop:config",
    "envoy.filters.http.compressor":                    "//source/extensions/filters/http/compressor:config",
    "envoy.filters.http.cors":                          "//source/extensions/filters/http/cors:config",
    "envoy.filters.http.composite":                     "//source/extensions/filters/http/composite:config",
    "envoy.filters.http.connect_grpc_bridge":           "//source/extensions/filters/http/connect_grpc_bridge:config",
    "envoy.filters.http.credential_injector":           "//source/extensions/filters/http/credential_injector:config",
    "envoy.filters.http.csrf":                          "//source/extensions/filters/http/csrf:config",
    "envoy.filters.http.custom_response":               "//source/extensions/filters/http/custom_response:factory",
    "envoy.filters.http.decompressor":                  "//source/extensions/filters/http/decompressor:config",
    "envoy.filters.http.dynamic_forward_proxy":         "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.ext_authz":                     "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.ext_proc":                      "//source/extensions/filters/http/ext_proc:config",
    "envoy.filters.http.fault":                         "//source/extensions/filters/http/fault:config",
    "envoy.filters.http.file_system_buffer":            "//source/extensions/filters/http/file_system_buffer:config",
    "envoy.filters.http.gcp_authn":                     "//source/extensions/filters/http/gcp_authn:config",
    "envoy.filters.http.geoip":                         "//source/extensions/filters/http/geoip:config",
    "envoy.filters.http.grpc_field_extraction":         "//source/extensions/filters/http/grpc_field_extraction:config",
    "envoy.filters.http.grpc_http1_bridge":             "//source/extensions/filters/http/grpc_http1_bridge:config",
    "envoy.filters.http.grpc_http1_reverse_bridge":     "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
    "envoy.filters.http.grpc_json_reverse_transcoder":  "//source/extensions/filters/http/grpc_json_reverse_transcoder:config",
    "envoy.filters.http.grpc_json_transcoder":          "//source/extensions/filters/http/grpc_json_transcoder:config",
    "envoy.filters.http.grpc_stats":                    "//source/extensions/filters/http/grpc_stats:config",
    "envoy.filters.http.grpc_web":                      "//source/extensions/filters/http/grpc_web:config",
    "envoy.filters.http.header_to_metadata":            "//source/extensions/filters/http/header_to_metadata:config",
    "envoy.filters.http.health_check":                  "//source/extensions/filters/http/health_check:config",
    "envoy.filters.http.ip_tagging":                    "//source/extensions/filters/http/ip_tagging:config",
    "envoy.filters.http.json_to_metadata":              "//source/extensions/filters/http/json_to_metadata:config",
    "envoy.filters.http.jwt_authn":                     "//source/extensions/filters/http/jwt_authn:config",
    "envoy.filters.http.mcp":                           "//source/extensions/filters/http/mcp:config",
    "envoy.filters.http.rate_limit_quota":              "//source/extensions/filters/http/rate_limit_quota:config",
    # Disabled by default. kill_request is not built into most prebuilt images.
    # For instructions for building with disabled-by-default filters enabled, see
    # https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#enabling-and-disabling-extensions
    "envoy.filters.http.kill_request":                  "//source/extensions/filters/http/kill_request:kill_request_config",
    "envoy.filters.http.local_ratelimit":               "//source/extensions/filters/http/local_ratelimit:config",
    "envoy.filters.http.lua":                           "//source/extensions/filters/http/lua:config",
    "envoy.filters.http.oauth2":                        "//source/extensions/filters/http/oauth2:config",
    "envoy.filters.http.on_demand":                     "//source/extensions/filters/http/on_demand:config",
    "envoy.filters.http.original_src":                  "//source/extensions/filters/http/original_src:config",
    "envoy.filters.http.proto_message_extraction":      "//source/extensions/filters/http/proto_message_extraction:config",
    "envoy.filters.http.ratelimit":                     "//source/extensions/filters/http/ratelimit:config",
    "envoy.filters.http.rbac":                          "//source/extensions/filters/http/rbac:config",
    "envoy.filters.http.router":                        "//source/extensions/filters/http/router:config",
    "envoy.filters.http.set_filter_state":              "//source/extensions/filters/http/set_filter_state:config",
    "envoy.filters.http.set_metadata":                  "//source/extensions/filters/http/set_metadata:config",
    "envoy.filters.http.tap":                           "//source/extensions/filters/http/tap:config",
    "envoy.filters.http.thrift_to_metadata":            "//source/extensions/filters/http/thrift_to_metadata:config",
    "envoy.filters.http.wasm":                          "//source/extensions/filters/http/wasm:config",
    "envoy.filters.http.stateful_session":              "//source/extensions/filters/http/stateful_session:config",
    "envoy.filters.http.header_mutation":               "//source/extensions/filters/http/header_mutation:config",
    "envoy.filters.http.transform":                     "//source/extensions/filters/http/transform:config",

    #
    # Listener filters
    #

    "envoy.filters.listener.http_inspector":            "//source/extensions/filters/listener/http_inspector:config",
    "envoy.filters.listener.local_ratelimit":           "//source/extensions/filters/listener/local_ratelimit:config",
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

    "envoy.filters.network.connection_limit":                     "//source/extensions/filters/network/connection_limit:config",
    "envoy.filters.network.direct_response":                      "//source/extensions/filters/network/direct_response:config",
    "envoy.filters.network.dubbo_proxy":                          "//source/extensions/filters/network/dubbo_proxy:config",
    "envoy.filters.network.echo":                                 "//source/extensions/filters/network/echo:config",
    "envoy.filters.network.ext_authz":                            "//source/extensions/filters/network/ext_authz:config",
    "envoy.filters.network.ext_proc":                             "//source/extensions/filters/network/ext_proc:config",
    "envoy.filters.network.reverse_tunnel":                       "//source/extensions/filters/network/reverse_tunnel:config",
    "envoy.filters.network.http_connection_manager":              "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.filters.network.local_ratelimit":                      "//source/extensions/filters/network/local_ratelimit:config",
    "envoy.filters.network.mongo_proxy":                          "//source/extensions/filters/network/mongo_proxy:config",
    "envoy.filters.network.ratelimit":                            "//source/extensions/filters/network/ratelimit:config",
    "envoy.filters.network.rbac":                                 "//source/extensions/filters/network/rbac:config",
    "envoy.filters.network.redis_proxy":                          "//source/extensions/filters/network/redis_proxy:config",
    "envoy.filters.network.tcp_proxy":                            "//source/extensions/filters/network/tcp_proxy:config",
    "envoy.filters.network.thrift_proxy":                         "//source/extensions/filters/network/thrift_proxy:config",
    "envoy.filters.network.set_filter_state":                     "//source/extensions/filters/network/set_filter_state:config",
    "envoy.filters.network.sni_cluster":                          "//source/extensions/filters/network/sni_cluster:config",
    "envoy.filters.network.sni_dynamic_forward_proxy":            "//source/extensions/filters/network/sni_dynamic_forward_proxy:config",
    "envoy.filters.network.wasm":                                 "//source/extensions/filters/network/wasm:config",
    "envoy.filters.network.zookeeper_proxy":                      "//source/extensions/filters/network/zookeeper_proxy:config",
    "envoy.filters.network.generic_proxy":                        "//source/extensions/filters/network/generic_proxy:config",

    #
    # UDP filters
    #

    "envoy.filters.udp.dns_filter":                     "//source/extensions/filters/udp/dns_filter:config",
    "envoy.filters.udp_listener.udp_proxy":             "//source/extensions/filters/udp/udp_proxy:config",

    #
    # UDP Session filters
    #

    "envoy.filters.udp.session.http_capsule":           "//source/extensions/filters/udp/udp_proxy/session_filters/http_capsule:config",
    "envoy.filters.udp.session.dynamic_forward_proxy":  "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy:config",

    #
    # Resource monitors
    #

    "envoy.resource_monitors.fixed_heap":               "//source/extensions/resource_monitors/fixed_heap:config",
    "envoy.resource_monitors.injected_resource":        "//source/extensions/resource_monitors/injected_resource:config",
    "envoy.resource_monitors.global_downstream_max_connections":   "//source/extensions/resource_monitors/downstream_connections:config",
    "envoy.resource_monitors.cpu_utilization":          "//source/extensions/resource_monitors/cpu_utilization:config",
    "envoy.resource_monitors.cgroup_memory":          "//source/extensions/resource_monitors/cgroup_memory:config",

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd":                      "//source/extensions/stat_sinks/dog_statsd:config",
    "envoy.stat_sinks.graphite_statsd":                 "//source/extensions/stat_sinks/graphite_statsd:config",
    "envoy.stat_sinks.hystrix":                         "//source/extensions/stat_sinks/hystrix:config",
    "envoy.stat_sinks.metrics_service":                 "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.stat_sinks.open_telemetry":                  "//source/extensions/stat_sinks/open_telemetry:config",
    "envoy.stat_sinks.statsd":                          "//source/extensions/stat_sinks/statsd:config",
    "envoy.stat_sinks.wasm":                            "//source/extensions/stat_sinks/wasm:config",

    #
    # Thrift filters
    #

    "envoy.filters.thrift.router":                      "//source/extensions/filters/network/thrift_proxy/router:config",
    "envoy.filters.thrift.header_to_metadata":          "//source/extensions/filters/network/thrift_proxy/filters/header_to_metadata:config",
    "envoy.filters.thrift.payload_to_metadata":         "//source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata:config",
    "envoy.filters.thrift.rate_limit":                  "//source/extensions/filters/network/thrift_proxy/filters/ratelimit:config",

    #
    # Tracers
    #

    "envoy.tracers.datadog":                            "//source/extensions/tracers/datadog:config",
    "envoy.tracers.zipkin":                             "//source/extensions/tracers/zipkin:config",
    "envoy.tracers.xray":                               "//source/extensions/tracers/xray:config",
    "envoy.tracers.skywalking":                         "//source/extensions/tracers/skywalking:config",
    "envoy.tracers.opentelemetry":                      "//source/extensions/tracers/opentelemetry:config",
    "envoy.tracers.fluentd":                            "//source/extensions/tracers/fluentd:config",

    #
    # OpenTelemetry Resource Detectors
    #

    "envoy.tracers.opentelemetry.resource_detectors.environment":         "//source/extensions/tracers/opentelemetry/resource_detectors/environment:config",
    "envoy.tracers.opentelemetry.resource_detectors.dynatrace":           "//source/extensions/tracers/opentelemetry/resource_detectors/dynatrace:config",
    "envoy.tracers.opentelemetry.resource_detectors.static_config":       "//source/extensions/tracers/opentelemetry/resource_detectors/static:config",

    #
    # OpenTelemetry tracer samplers
    #

    "envoy.tracers.opentelemetry.samplers.cel":                           "//source/extensions/tracers/opentelemetry/samplers/cel:config",
    "envoy.tracers.opentelemetry.samplers.always_on":                     "//source/extensions/tracers/opentelemetry/samplers/always_on:config",
    "envoy.tracers.opentelemetry.samplers.dynatrace":                     "//source/extensions/tracers/opentelemetry/samplers/dynatrace:config",
    "envoy.tracers.opentelemetry.samplers.parent_based":                  "//source/extensions/tracers/opentelemetry/samplers/parent_based:config",
    "envoy.tracers.opentelemetry.samplers.trace_id_ratio_based":          "//source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based:config",

    #
    # Transport sockets
    #

    "envoy.transport_sockets.alts":                     "//source/extensions/transport_sockets/alts:config",
    "envoy.transport_sockets.http_11_proxy":            "//source/extensions/transport_sockets/http_11_proxy:upstream_config",
    "envoy.transport_sockets.upstream_proxy_protocol":  "//source/extensions/transport_sockets/proxy_protocol:upstream_config",
    "envoy.transport_sockets.raw_buffer":               "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tap":                      "//source/extensions/transport_sockets/tap:config",
    "envoy.transport_sockets.starttls":                 "//source/extensions/transport_sockets/starttls:config",
    "envoy.transport_sockets.tcp_stats":                "//source/extensions/transport_sockets/tcp_stats:config",
    "envoy.transport_sockets.tls":                      "//source/extensions/transport_sockets/tls:config",
    "envoy.transport_sockets.internal_upstream":        "//source/extensions/transport_sockets/internal_upstream:config",

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
    "envoy.extensions.http.cache.file_system_http_cache":    "//source/extensions/http/cache/file_system_http_cache:config",
    "envoy.extensions.http.cache.simple":                    "//source/extensions/http/cache/simple_http_cache:config",
    "envoy.extensions.http.cache_v2.file_system_http_cache": "//source/extensions/http/cache_v2/file_system_http_cache:config",
    "envoy.extensions.http.cache_v2.simple":                 "//source/extensions/http/cache_v2/simple_http_cache:config",

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
    "envoy.upstreams.http.udp":                         "//source/extensions/upstreams/http/udp:config",

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
    "envoy.wasm.runtime.wasmtime":                      "//source/extensions/wasm_runtime/wasmtime:config",

    #
    # Rate limit descriptors
    #

    "envoy.rate_limit_descriptors.expr":                "//source/extensions/rate_limit_descriptors/expr:config",

    #
    # IO socket
    #

    "envoy.io_socket.user_space":                       "//source/extensions/io_socket/user_space:config",
    "envoy.bootstrap.internal_listener":                "//source/extensions/bootstrap/internal_listener:config",

    #
    # TLS peer certification validators
    #

    "envoy.tls.cert_validator.spiffe":                  "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",

    #
    # HTTP header formatters
    #

    "envoy.http.stateful_header_formatters.preserve_case":       "//source/extensions/http/header_formatters/preserve_case:config",

    #
    # Original IP detection
    #

    "envoy.http.original_ip_detection.custom_header":        "//source/extensions/http/original_ip_detection/custom_header:config",
    "envoy.http.original_ip_detection.xff":                  "//source/extensions/http/original_ip_detection/xff:config",

    #
    # Stateful session
    #

    "envoy.http.stateful_session.cookie":                "//source/extensions/http/stateful_session/cookie:config",
    "envoy.http.stateful_session.envelope":              "//source/extensions/http/stateful_session/envelope:config",
    "envoy.http.stateful_session.header":                "//source/extensions/http/stateful_session/header:config",

    #
    # Custom response policies
    #

    "envoy.http.custom_response.redirect_policy":             "//source/extensions/http/custom_response/redirect_policy:redirect_policy_lib",
    "envoy.http.custom_response.local_response_policy":       "//source/extensions/http/custom_response/local_response_policy:local_response_policy_lib",

    #
    # External Processing Request Modifiers
    #
    "envoy.http.ext_proc.processing_request_modifiers.mapped_attribute_builder":         "//source/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder:mapped_attribute_builder_lib",

    #
    # External Processing Response Processors
    #
    "envoy.http.ext_proc.response_processors.save_processing_response":         "//source/extensions/http/ext_proc/response_processors/save_processing_response:save_processing_response_lib",

    #
    # Injected credentials
    #

    "envoy.http.injected_credentials.generic":              "//source/extensions/http/injected_credentials/generic:config",
    "envoy.http.injected_credentials.oauth2":               "//source/extensions/http/injected_credentials/oauth2:config",

    #
    # QUIC extensions
    #

    "envoy.quic.deterministic_connection_id_generator": "//source/extensions/quic/connection_id_generator/deterministic:envoy_deterministic_connection_id_generator_config",
    "envoy.quic.connection_id_generator.quic_lb":       "//source/extensions/quic/connection_id_generator/quic_lb:quic_lb_config",
    "envoy.quic.crypto_stream.server.quiche":           "//source/extensions/quic/crypto_stream:envoy_quic_default_crypto_server_stream",
    "envoy.quic.proof_source.filter_chain":             "//source/extensions/quic/proof_source:envoy_quic_default_proof_source",
    "envoy.quic.server_preferred_address.fixed":        "//source/extensions/quic/server_preferred_address:fixed_server_preferred_address_config_factory_config",
    "envoy.quic.server_preferred_address.datasource":   "//source/extensions/quic/server_preferred_address:datasource_server_preferred_address_config_factory_config",
    "envoy.quic.connection_debug_visitor.basic":        "//source/extensions/quic/connection_debug_visitor/basic:envoy_quic_connection_debug_visitor_basic",
    "envoy.quic.connection_debug_visitor.quic_stats":   "//source/extensions/quic/connection_debug_visitor/quic_stats:config",

    #
    # UDP packet writers
    #
    "envoy.udp_packet_writer.default":                  "//source/extensions/udp_packet_writer/default:config",
    "envoy.udp_packet_writer.gso":                      "//source/extensions/udp_packet_writer/gso:config",

    #
    # Formatter
    #

    "envoy.formatter.cel":                              "//source/extensions/formatter/cel:config",
    "envoy.formatter.metadata":                         "//source/extensions/formatter/metadata:config",
    "envoy.formatter.req_without_query":                "//source/extensions/formatter/req_without_query:config",
    "envoy.built_in_formatters.xfcc_value":             "//source/extensions/formatter/xfcc_value:config",

    #
    # Key value store
    #

    "envoy.key_value.file_based":     "//source/extensions/key_value/file_based:config_lib",

    #
    # RBAC matchers
    #

    "envoy.rbac.matchers.upstream_ip_port":     "//source/extensions/filters/common/rbac/matchers:upstream_ip_port_lib",

    #
    # RBAC principals
    #

    "envoy.rbac.principals.mtls_authenticated":        "//source/extensions/filters/common/rbac/principals/mtls_authenticated:config",

    #
    # DNS Resolver
    #

    # c-ares DNS resolver extension is recommended to be enabled to maintain the legacy DNS resolving behavior.
    "envoy.network.dns_resolver.cares":                "//source/extensions/network/dns_resolver/cares:config",
    # apple DNS resolver extension is only needed in MacOS build plus one want to use apple library for DNS resolving.
    "envoy.network.dns_resolver.apple":                "//source/extensions/network/dns_resolver/apple:config",
    # getaddrinfo DNS resolver extension can be used when the system resolver is desired (e.g., Android)
    "envoy.network.dns_resolver.getaddrinfo":          "//source/extensions/network/dns_resolver/getaddrinfo:config",

    #
    # Address Resolvers
    #

    "envoy.resolvers.reverse_connection":               "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_resolver_lib",

    #
    # Custom matchers
    #

    "envoy.matching.custom_matchers.ip_range_matcher":     "//source/extensions/common/matcher:ip_range_matcher_lib",
    "envoy.matching.custom_matchers.domain_matcher":   "//source/extensions/common/matcher:domain_matcher_lib",

    #
    # Header Validators
    #

    "envoy.http.header_validators.envoy_default":        "//source/extensions/http/header_validators/envoy_default:config",

    #
    # Path Pattern Match and Path Pattern Rewrite
    #
    "envoy.path.match.uri_template.uri_template_matcher": "//source/extensions/path/match/uri_template:config",
    "envoy.path.rewrite.uri_template.uri_template_rewriter": "//source/extensions/path/rewrite/uri_template:config",
    #
    # Early Data option
    #

    "envoy.route.early_data_policy.default":           "//source/extensions/early_data:default_early_data_policy_lib",

    #
    # Load balancing policies for upstream
    #
    "envoy.load_balancing_policies.least_request":     "//source/extensions/load_balancing_policies/least_request:config",
    "envoy.load_balancing_policies.random":            "//source/extensions/load_balancing_policies/random:config",
    "envoy.load_balancing_policies.round_robin":       "//source/extensions/load_balancing_policies/round_robin:config",
    "envoy.load_balancing_policies.maglev":            "//source/extensions/load_balancing_policies/maglev:config",
    "envoy.load_balancing_policies.ring_hash":         "//source/extensions/load_balancing_policies/ring_hash:config",
    "envoy.load_balancing_policies.subset":            "//source/extensions/load_balancing_policies/subset:config",
    "envoy.load_balancing_policies.cluster_provided":  "//source/extensions/load_balancing_policies/cluster_provided:config",
    "envoy.load_balancing_policies.client_side_weighted_round_robin": "//source/extensions/load_balancing_policies/client_side_weighted_round_robin:config",
    "envoy.load_balancing_policies.override_host":     "//source/extensions/load_balancing_policies/override_host:config",
    "envoy.load_balancing_policies.wrr_locality":      "//source/extensions/load_balancing_policies/wrr_locality:config",

    #
    # HTTP Early Header Mutation
    #
    "envoy.http.early_header_mutation.header_mutation": "//source/extensions/http/early_header_mutation/header_mutation:config",

    #
    # Config Subscription
    #
    "envoy.config_subscription.rest": "//source/extensions/config_subscription/rest:http_subscription_lib",
    "envoy.config_subscription.filesystem": "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
    "envoy.config_subscription.filesystem_collection": "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
    "envoy.config_subscription.grpc": "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
    "envoy.config_subscription.delta_grpc": "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
    "envoy.config_subscription.ads": "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
    "envoy.config_subscription.aggregated_grpc_collection": "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
    "envoy.config_subscription.aggregated_delta_grpc_collection": "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
    "envoy.config_subscription.ads_collection": "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
    "envoy.config_mux.delta_grpc_mux_factory": "//source/extensions/config_subscription/grpc/xds_mux:grpc_mux_lib",
    "envoy.config_mux.sotw_grpc_mux_factory": "//source/extensions/config_subscription/grpc/xds_mux:grpc_mux_lib",

    #
    # Geolocation Provider
    #
    "envoy.geoip_providers.maxmind":                         "//source/extensions/geoip_providers/maxmind:config",

    #
    # cluster specifier plugin
    #
    "envoy.router.cluster_specifier_plugin.lua":     "//source/extensions/router/cluster_specifiers/lua:config",
    "envoy.router.cluster_specifier_plugin.matcher": "//source/extensions/router/cluster_specifiers/matcher:config",

    #
    # Extensions for generic proxy
    #
    "envoy.filters.generic.router":                             "//source/extensions/filters/network/generic_proxy/router:config",
    "envoy.generic_proxy.codecs.dubbo":                         "//source/extensions/filters/network/generic_proxy/codecs/dubbo:config",
    "envoy.generic_proxy.codecs.http1":                         "//source/extensions/filters/network/generic_proxy/codecs/http1:config",

    # Dynamic mocules
    "envoy.filters.http.dynamic_modules":                      "//source/extensions/filters/http/dynamic_modules:factory_registration",
}

# These can be changed to ["//visibility:public"], for  downstream builds which
# need to directly reference Envoy extensions.
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config", "//:contrib_library", "//:mobile_library"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library", "//:contrib_library", "//:mobile_library"]
CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["//:contrib_library"]
MOBILE_PACKAGE_VISIBILITY = ["//:mobile_library"]

# Set this variable to true to disable alwayslink for envoy_cc_library.
# TODO(alyssawilk) audit uses of this in source/ and migrate all libraries to extensions.
LEGACY_ALWAYSLINK = 1
