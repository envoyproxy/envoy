load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# Return all extensions to be compiled into Envoy.
# TODO(mattklein123): Figure out a way to output in the build and possibly even the binary which
#                     extensions are statically registered.
def envoy_all_extensions():
  # These extensions are registered using the extension system but are required for the core
  # Envoy build.
  all_extensions = [
    "@envoy//source/extensions/access_loggers/file:config",
    "@envoy//source/extensions/access_loggers/http_grpc:config",
    "@envoy//source/extensions/filters/http/dynamo:config",
    "@envoy//source/extensions/filters/http/ext_authz:config",
    "@envoy//source/extensions/filters/http/ratelimit:config",
    "@envoy//source/extensions/filters/listener/proxy_protocol:config",
    "@envoy//source/extensions/filters/listener/original_dst:config",
    "@envoy//source/extensions/filters/network/client_ssl_auth:config",
    "@envoy//source/extensions/filters/network/echo:config",
    "@envoy//source/extensions/filters/network/ext_authz:config",
    "@envoy//source/extensions/filters/network/mongo_proxy:config",
    "@envoy//source/extensions/filters/network/ratelimit:config",
    "@envoy//source/extensions/filters/network/redis_proxy:config",
    "@envoy//source/extensions/filters/network/tcp_proxy:config",
    "@envoy//source/extensions/stat_sinks/dog_statsd:config",
    "@envoy//source/extensions/stat_sinks/metrics_service:config",
    "@envoy//source/extensions/stat_sinks/statsd:config",
    "@envoy//source/extensions/tracers/dynamic_ot:config",
    "@envoy//source/extensions/tracers/lightstep:config",
    "@envoy//source/extensions/tracers/zipkin:config",
    "@envoy//source/extensions/transport_sockets/raw_buffer:config",
    "@envoy//source/extensions/transport_sockets/ssl:config",
  ]

  # These extensions can be removed on a site specific basis.
  for path in EXTENSIONS.values():
    all_extensions.append(path)

  return all_extensions



