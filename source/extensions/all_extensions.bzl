load("@envoy_build_config//:extensions_build_config.bzl", "ENVOY_BUILD_CONFIG")

# Return all extensions to be compiled into Envoy.
# TODO(mattklein123): Figure out a way to output in the build and possibly even the binary which
#                     extensions are statically registered.
def envoy_all_extensions(repository = ""):
  # These extensions are registered using the extension system but are required for the core
  # Envoy build.
  all_extensions = [
    repository + "//source/extensions/access_loggers/file:config",
    repository + "//source/extensions/access_loggers/http_grpc:config",
    repository + "//source/extensions/filters/http/dynamo:config",
    repository + "//source/extensions/filters/http/ext_authz:config",
    repository + "//source/extensions/filters/http/ratelimit:config",
    repository + "//source/extensions/filters/listener/proxy_protocol:config",
    repository + "//source/extensions/filters/listener/original_dst:config",
    repository + "//source/extensions/filters/network/client_ssl_auth:config",
    repository + "//source/extensions/filters/network/echo:config",
    repository + "//source/extensions/filters/network/ext_authz:config",
    repository + "//source/extensions/filters/network/mongo_proxy:config",
    repository + "//source/extensions/filters/network/ratelimit:config",
    repository + "//source/extensions/filters/network/redis_proxy:config",
    repository + "//source/extensions/filters/network/tcp_proxy:config",
    repository + "//source/extensions/stat_sinks/dog_statsd:config",
    repository + "//source/extensions/stat_sinks/metrics_service:config",
    repository + "//source/extensions/stat_sinks/statsd:config",
    repository + "//source/extensions/tracers/dynamic_ot:config",
    repository + "//source/extensions/tracers/lightstep:config",
    repository + "//source/extensions/tracers/zipkin:config",
    repository + "//source/extensions/transport_sockets/raw_buffer:config",
    repository + "//source/extensions/transport_sockets/ssl:config",
  ]

  # These extensions can be removed on a site specific basis.
  for name, config in ENVOY_BUILD_CONFIG.items():
    if config["enabled"]:
      all_extensions.append(repository + config["path"])

  return all_extensions



