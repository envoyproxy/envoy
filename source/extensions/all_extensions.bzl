# Return all extensions to be compiled into Envoy.
# TODO(mattklein123): Every extension should have an independent Bazel select option that will
# allow us to compile in and out different extensions. We may also consider in the future other
# selection options such as maturity.
def envoy_all_extensions(repository = ""):
  return [
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
  ]

