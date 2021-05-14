# See bazel/README.md for details on how this system works.
EXTENSIONS = {

    "envoy.request_id.uuid": {
        "source": "//source/extensions/request_id/uuid:config",
        "categories": ["envoy.request_id"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
        "builtin": True,
        "required": True,
    },
    "envoy.common.crypto.utility_lib": {
        "source": "//source/extensions/common/crypto:utility_lib",
        "required": True,
        "categories": ["DELIBERATELY_OMITTED"],
        "security_posture": "unknown",
        "undocumented": True,
    },

    #
    # Access loggers
    #

    "envoy.access_loggers.file": {
        "source": "//source/extensions/access_loggers/file:config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "robust_to_untrusted_downstream",
        "core": True,
    },
    "envoy.access_loggers.http_grpc": {
        "source": "//source/extensions/access_loggers/grpc:http_config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.access_loggers.tcp_grpc": {
        "source": "//source/extensions/access_loggers/grpc:tcp_config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.access_loggers.open_telemetry": {
        "source": "//source/extensions/access_loggers/open_telemetry:config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.access_loggers.stream": {
        "source": "//source/extensions/access_loggers/stream:config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "robust_to_untrusted_downstream",
        "core": True,
    },
    "envoy.access_loggers.wasm": {
        "source": "//source/extensions/access_loggers/wasm:config",
        "categories": ["envoy.access_loggers"],
        "security_posture": "unknown",
        "status": "alpha",
    },

    #
    # Clusters
    #

    "envoy.clusters.aggregate": {
        "source": "//source/extensions/clusters/aggregate:cluster",
        "categories": ["envoy.clusters"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.clusters.dynamic_forward_proxy": {
        "source": "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "categories": ["envoy.clusters"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.clusters.redis": {
        "source": "//source/extensions/clusters/redis:redis_cluster",
        "categories": ["envoy.clusters"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },

    #
    # Compression
    #

    "envoy.compression.gzip.compressor": {
        "source": "//source/extensions/compression/gzip/compressor:config",
        "categories": ["envoy.compression.compressor"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.compression.gzip.decompressor": {
        "source": "//source/extensions/compression/gzip/decompressor:config",
        "categories": ["envoy.compression.decompressor"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.compression.brotli.compressor": {
        "source": "//source/extensions/compression/brotli/compressor:config",
        "categories": ["envoy.compression.compressor"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.compression.brotli.decompressor": {
        "source": "//source/extensions/compression/brotli/decompressor:config",
        "categories": ["envoy.compression.decompressor"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # gRPC Credentials Plugins
    #

    "envoy.grpc_credentials.file_based_metadata": {
        "source": "//source/extensions/grpc_credentials/file_based_metadata:config",
        "categories": ["envoy.grpc_credentials"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },
    "envoy.grpc_credentials.aws_iam": {
        "source": "//source/extensions/grpc_credentials/aws_iam:config",
        "categories": ["envoy.grpc_credentials"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },

    #
    # WASM
    #

    "envoy.bootstrap.wasm": {
        "source": "//source/extensions/bootstrap/wasm:config",
        "categories": ["envoy.bootstrap"],
        "security_posture": "unknown",
        "status": "alpha",
    },

    #
    # Health checkers
    #

    "envoy.health_checkers.redis": {
        "source": "//source/extensions/health_checkers/redis:config",
        "categories": ["envoy.health_checkers"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },

    #
    # Input Matchers
    #

    "envoy.matching.input_matchers.consistent_hashing": {
        "source": "//source/extensions/matching/input_matchers/consistent_hashing:config",
        "categories": ["envoy.matching.input_matchers"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # Generic Inputs
    #

    "envoy.matching.common_inputs.environment_variable": {
        "source": "//source/extensions/matching/common_inputs/environment_variable:config",
        "categories": ["envoy.matching.common_inputs"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # HTTP filters
    #

    "envoy.filters.http.adaptive_concurrency": {
        "source": "//source/extensions/filters/http/adaptive_concurrency:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.admission_control": {
        "source": "//source/extensions/filters/http/admission_control:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.aws_lambda": {
        "source": "//source/extensions/filters/http/aws_lambda:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.http.aws_request_signing": {
        "source": "//source/extensions/filters/http/aws_request_signing:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.http.buffer": {
        "source": "//source/extensions/filters/http/buffer:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.cache": {
        "source": "//source/extensions/filters/http/cache:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
        "status": "wip",
    },
    "envoy.filters.http.cdn_loop": {
        "source": "//source/extensions/filters/http/cdn_loop:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.compressor": {
        "source": "//source/extensions/filters/http/compressor:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.cors": {
        "source": "//source/extensions/filters/http/cors:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.composite": {
        "source": "//source/extensions/filters/http/composite:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.csrf": {
        "source": "//source/extensions/filters/http/csrf:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.decompressor": {
        "source": "//source/extensions/filters/http/decompressor:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
    "envoy.filters.http.dynamic_forward_proxy": {
        "source": "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.dynamo": {
        "source": "//source/extensions/filters/http/dynamo:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.http.ext_authz": {
        "source": "//source/extensions/filters/http/ext_authz:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.ext_proc": {
        "source": "//source/extensions/filters/http/ext_proc:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.fault": {
        "source": "//source/extensions/filters/http/fault:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.grpc_http1_bridge": {
        "source": "//source/extensions/filters/http/grpc_http1_bridge:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
    },
    "envoy.filters.http.grpc_http1_reverse_bridge": {
        "source": "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.grpc_json_transcoder": {
        "source": "//source/extensions/filters/http/grpc_json_transcoder:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.grpc_stats": {
        "source": "//source/extensions/filters/http/grpc_stats:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.http.grpc_web": {
        "source": "//source/extensions/filters/http/grpc_web:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.gzip": {
        "source": "//source/extensions/filters/http/gzip:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.header_to_metadata": {
        "source": "//source/extensions/filters/http/header_to_metadata:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.health_check": {
        "source": "//source/extensions/filters/http/health_check:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
        "core": True,
    },
    "envoy.filters.http.ip_tagging": {
        "source": "//source/extensions/filters/http/ip_tagging:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.jwt_authn": {
        "source": "//source/extensions/filters/http/jwt_authn:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "alpha",
    },
    # Disabled by default
    "envoy.filters.http.kill_request": {
        "source": "//source/extensions/filters/http/kill_request:kill_request_config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.local_ratelimit": {
        "source": "//source/extensions/filters/http/local_ratelimit:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
    },
    "envoy.filters.http.lua": {
        "source": "//source/extensions/filters/http/lua:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.oauth2": {
        "source": "//source/extensions/filters/http/oauth2:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "alpha",
    },
    "envoy.filters.http.on_demand": {
        "source": "//source/extensions/filters/http/on_demand:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.original_src": {
        "source": "//source/extensions/filters/http/original_src:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "alpha",
    },
    "envoy.filters.http.ratelimit": {
        "source": "//source/extensions/filters/http/ratelimit:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.rbac": {
        "source": "//source/extensions/filters/http/rbac:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.http.router": {
        "source": "//source/extensions/filters/http/router:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "robust_to_untrusted_downstream",
        "core": True,
    },
    "envoy.filters.http.squash": {
        "source": "//source/extensions/filters/http/squash:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.http.tap": {
        "source": "//source/extensions/filters/http/tap:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.http.wasm": {
        "source": "//source/extensions/filters/http/wasm:config",
        "categories": ["envoy.filters.http"],
        "security_posture": "unknown",
        "status": "alpha",
    },

    #
    # Listener filters
    #

    "envoy.filters.listener.http_inspector": {
        "source": "//source/extensions/filters/listener/http_inspector:config",
        "categories": ["envoy.filters.listener"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    # NOTE: The original_dst filter is implicitly loaded if original_dst functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.original_dst": {
        "source": "//source/extensions/filters/listener/original_dst:config",
        "categories": ["envoy.filters.listener"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.listener.original_src": {
        "source": "//source/extensions/filters/listener/original_src:config",
        "categories": ["envoy.filters.listener"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "alpha",
    },
    # NOTE: The proxy_protocol filter is implicitly loaded if proxy_protocol functionality is
    #       configured on the listener. Do not remove it in that case or configs will fail to load.
    "envoy.filters.listener.proxy_protocol": {
        "source": "//source/extensions/filters/listener/proxy_protocol:config",
        "categories": ["envoy.filters.listener"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.listener.tls_inspector": {
        "source": "//source/extensions/filters/listener/tls_inspector:config",
        "categories": ["envoy.filters.listener"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # Network filters
    #

    "envoy.filters.network.client_ssl_auth": {
        "source": "//source/extensions/filters/network/client_ssl_auth:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.direct_response": {
        "source": "//source/extensions/filters/network/direct_response:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "unknown",
    },
    "envoy.filters.network.dubbo_proxy": {
        "source": "//source/extensions/filters/network/dubbo_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.network.echo": {
        "source": "//source/extensions/filters/network/echo:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "unknown",
    },
    "envoy.filters.network.ext_authz": {
        "source": "//source/extensions/filters/network/ext_authz:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.http_connection_manager": {
        "source": "//source/extensions/filters/network/http_connection_manager:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
        "core": True,
    },
    "envoy.filters.network.kafka_broker": {
        "source": "//source/extensions/filters/network/kafka:kafka_broker_config_lib",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "wip",
    },
    "envoy.filters.network.local_ratelimit": {
        "source": "//source/extensions/filters/network/local_ratelimit:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.mongo_proxy": {
        "source": "//source/extensions/filters/network/mongo_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.network.mysql_proxy": {
        "source": "//source/extensions/filters/network/mysql_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.network.postgres_proxy": {
        "source": "//source/extensions/filters/network/postgres_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.network.ratelimit": {
        "source": "//source/extensions/filters/network/ratelimit:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.rbac": {
        "source": "//source/extensions/filters/network/rbac:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.redis_proxy": {
        "source": "//source/extensions/filters/network/redis_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.network.rocketmq_proxy": {
        "source": "//source/extensions/filters/network/rocketmq_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.filters.network.tcp_proxy": {
        "source": "//source/extensions/filters/network/tcp_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.filters.network.thrift_proxy": {
        "source": "//source/extensions/filters/network/thrift_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.network.sni_cluster": {
        "source": "//source/extensions/filters/network/sni_cluster:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "unknown",
    },
    "envoy.filters.network.sni_dynamic_forward_proxy": {
        "source": "//source/extensions/filters/network/sni_dynamic_forward_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.network.wasm": {
        "source": "//source/extensions/filters/network/wasm:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.filters.network.zookeeper_proxy": {
        "source": "//source/extensions/filters/network/zookeeper_proxy:config",
        "categories": ["envoy.filters.network"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },

    #
    # UDP filters
    #

    "envoy.filters.udp_listener.dns_filter": {
        "source": "//source/extensions/filters/udp/dns_filter:config",
        "categories": ["envoy.filters.udp_listener"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "alpha",
    },
    "envoy.filters.udp_listener.udp_proxy": {
        "source": "//source/extensions/filters/udp/udp_proxy:config",
        "categories": ["envoy.filters.udp_listener"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # Resource monitors
    #

    "envoy.resource_monitors.fixed_heap": {
        "source": "//source/extensions/resource_monitors/fixed_heap:config",
        "categories": ["envoy.resource_monitors"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },
    "envoy.resource_monitors.injected_resource": {
        "source": "//source/extensions/resource_monitors/injected_resource:config",
        "categories": ["envoy.resource_monitors"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },

    #
    # Stat sinks
    #

    "envoy.stat_sinks.dog_statsd": {
        "source": "//source/extensions/stat_sinks/dog_statsd:config",
        "categories": ["envoy.stats_sinks"],
        "security_posture": "data_plane_agnostic",
    },
    "envoy.stat_sinks.hystrix": {
        "source": "//source/extensions/stat_sinks/hystrix:config",
        "categories": ["envoy.stats_sinks"],
        "security_posture": "data_plane_agnostic",
    },
    "envoy.stat_sinks.metrics_service": {
        "source": "//source/extensions/stat_sinks/metrics_service:config",
        "categories": ["envoy.stats_sinks"],
        "security_posture": "data_plane_agnostic",
    },
    "envoy.stat_sinks.statsd": {
        "source": "//source/extensions/stat_sinks/statsd:config",
        "categories": ["envoy.stats_sinks"],
        "security_posture": "data_plane_agnostic",
        "core": True,
    },
    "envoy.stat_sinks.wasm": {
        "source": "//source/extensions/stat_sinks/wasm:config",
        "categories": ["envoy.stats_sinks"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },

    #
    # Thrift filters
    #

    "envoy.filters.thrift.router": {
        "source": "//source/extensions/filters/network/thrift_proxy/router:config",
        "categories": ["envoy.thrift_proxy.filters"],
        "security_posture": "requires_trusted_downstream_and_upstream",
    },
    "envoy.filters.thrift.ratelimit": {
        "source": "//source/extensions/filters/network/thrift_proxy/filters/ratelimit:config",
        "categories": ["envoy.thrift_proxy.filters"],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },

    #
    # Tracers
    #

    "envoy.tracers.dynamic_ot": {
        "source": "//source/extensions/tracers/dynamic_ot:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.lightstep": {
        "source": "//source/extensions/tracers/lightstep:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.datadog": {
        "source": "//source/extensions/tracers/datadog:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.zipkin": {
        "source": "//source/extensions/tracers/zipkin:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.opencensus": {
        "source": "//source/extensions/tracers/opencensus:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.xray": {
        "source": "//source/extensions/tracers/xray:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.tracers.skywalking": {
        "source": "//source/extensions/tracers/skywalking:config",
        "categories": ["envoy.tracers"],
        "security_posture": "robust_to_untrusted_downstream",
        "status": "wip",
    },

    #
    # Transport sockets
    #

    "envoy.transport_sockets.tls": {
        "source": "//source/extensions/transport_sockets/tls:config",
        "categories": [
            "envoy.transport_sockets.downstream",
            "envoy.transport_sockets.upstream",
        ],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
        "builtin": True,
        "required": True,
    },
    "envoy.transport_sockets.alts": {
        "source": "//source/extensions/transport_sockets/alts:config",
        "categories": [
            "envoy.transport_sockets.downstream",
            "envoy.transport_sockets.upstream",
        ],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
    "envoy.transport_sockets.upstream_proxy_protocol": {
        "source": "//source/extensions/transport_sockets/proxy_protocol:upstream_config",
        "categories": ["envoy.transport_sockets.upstream"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
    "envoy.transport_sockets.raw_buffer": {
        "source": "//source/extensions/transport_sockets/raw_buffer:config",
        "categories": [
            "envoy.transport_sockets.downstream",
            "envoy.transport_sockets.upstream",
        ],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "core": True,
    },
    "envoy.transport_sockets.tap": {
        "source": "//source/extensions/transport_sockets/tap:config",
        "categories": [
            "envoy.transport_sockets.downstream",
            "envoy.transport_sockets.upstream",
        ],
        "security_posture": "requires_trusted_downstream_and_upstream",
        "status": "alpha",
    },
    "envoy.transport_sockets.starttls": {
        "source": "//source/extensions/transport_sockets/starttls:config",
        "categories": [
            "envoy.transport_sockets.downstream",
            "envoy.transport_sockets.upstream",
        ],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },

    #
    # Retry host predicates
    #

    "envoy.retry_host_predicates.previous_hosts": {
        "source": "//source/extensions/retry/host/previous_hosts:config",
        "categories": ["envoy.retry_host_predicates"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.retry_host_predicates.omit_canary_hosts": {
        "source": "//source/extensions/retry/host/omit_canary_hosts:config",
        "categories": ["envoy.retry_host_predicates"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.retry_host_predicates.omit_host_metadata": {
        "source": "//source/extensions/retry/host/omit_host_metadata:config",
        "categories": ["envoy.retry_host_predicates"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # Retry priorities
    #

    "envoy.retry_priorities.previous_priorities": {
        "source": "//source/extensions/retry/priority/previous_priorities:config",
        "categories": ["envoy.retry_priorities"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # CacheFilter plugins
    #

    "envoy.cache.simple_http_cache": {
        "source": "//source/extensions/filters/http/cache/simple_http_cache:config",
        "categories": ["envoy.filters.http.cache"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
        "status": "wip",
    },

    #
    # Internal redirect predicates
    #

    "envoy.internal_redirect_predicates.allow_listed_routes": {
        "source": "//source/extensions/internal_redirect/allow_listed_routes:config",
        "categories": ["envoy.internal_redirect_predicates"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
    "envoy.internal_redirect_predicates.previous_routes": {
        "source": "//source/extensions/internal_redirect/previous_routes:config",
        "categories": ["envoy.internal_redirect_predicates"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
    "envoy.internal_redirect_predicates.safe_cross_scheme":   {
        "source": "//source/extensions/internal_redirect/safe_cross_scheme:config",
        "categories": ["envoy.internal_redirect_predicates"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },

    #
    # TCP Upstreams
    #

    "envoy.upstreams.tcp.generic": {
        "source": "//source/extensions/upstreams/tcp/generic:config",
        "categories": ["envoy.upstreams"],
        "security_posture": "robust_to_untrusted_downstream",
        "builtin": True,
    },

    #
    # Http Upstreams
    #

    "envoy.upstreams.http.http_protocol_options": {
        "source": "//source/extensions/upstreams/http:config",
        "categories": ["envoy.upstreams"],
        "security_posture": "robust_to_untrusted_downstream",
        "builtin": True,
    },
    "envoy.upstreams.http.generic": {
        "source": "//source/extensions/upstreams/http/generic:config",
        "categories": ["envoy.upstreams"],
        "security_posture": "robust_to_untrusted_downstream",
        "builtin": True,
    },
    "envoy.upstreams.http.http": {
        "source": "//source/extensions/upstreams/http/http:config",
        "categories": ["envoy.upstreams"],
        "security_posture": "robust_to_untrusted_downstream",
    },
    "envoy.upstreams.http.tcp": {
        "source": "//source/extensions/upstreams/http/tcp:config",
        "categories": ["envoy.upstreams"],
        "security_posture": "robust_to_untrusted_downstream",
    },

    #
    # Watchdog actions
    #

    "envoy.watchdog.profile_action": {
        "source": "//source/extensions/watchdog/profile_action:config",
        "categories": ["envoy.guarddog_actions"],
        "security_posture": "data_plane_agnostic",
        "status": "alpha",
    },

    #
    # WebAssembly runtimes
    #

    "envoy.wasm.runtime.null": {
        "source": "//source/extensions/wasm_runtime/null:config",
        "categories": ["envoy.wasm.runtime"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.wasm.runtime.v8": {
        "source": "//source/extensions/wasm_runtime/v8:config",
        "categories": ["envoy.wasm.runtime"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.wasm.runtime.wavm": {
        "source": "//source/extensions/wasm_runtime/wavm:config",
        "categories": ["envoy.wasm.runtime"],
        "security_posture": "unknown",
        "status": "alpha",
    },
    "envoy.wasm.runtime.wasmtime": {
        "source": "//source/extensions/wasm_runtime/wasmtime:config",
        "categories": ["envoy.wasm.runtime"],
        "security_posture": "unknown",
        "status": "alpha",
    },

    #
    # Rate limit descriptors
    #

    "envoy.rate_limit_descriptors.expr": {
        "source": "//source/extensions/rate_limit_descriptors/expr:config",
        "categories": ["envoy.rate_limit_descriptors"],
        "security_posture": "unknown",
    },

    #
    # IO socket
    #

    "envoy.io_socket.user_space": {
        "source": "//source/extensions/io_socket/user_space:config",
        "categories": ["envoy.io_socket"],
        "security_posture": "unknown",
        "status": "wip",
        "undocumented": True,
    },

    #
    # TLS peer certification validators
    #

    "envoy.tls.cert_validator.spiffe": {
        "source": "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "categories": ["envoy.tls.cert_validator"],
        "security_posture": "unknown",
        "status": "wip",
    },

    #
    # HTTP header formatters
    #

    "envoy.http.stateful_header_formatters.preserve_case": {
        "source": "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "categories": ["envoy.http.stateful_header_formatters"],
        "security_posture": "robust_to_untrusted_downstream_and_upstream",
    },
}

# These can be changed to ["//visibility:public"], for  downstream builds which
# need to directly reference Envoy extensions.
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library"]
