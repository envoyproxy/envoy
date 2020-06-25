load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# These extensions are registered using the extension system but are required for the core Envoy build.
# The map may be overridden by extensions specified in envoy_build_config.
_required_extensions = {
    "envoy.common.crypto.utility_lib": "//source/extensions/common/crypto:utility_lib",
    "envoy.transport_sockets.tls": "//source/extensions/transport_sockets/tls:config",
}

# Return all extensions to be compiled into Envoy.
def envoy_all_extensions(denylist = []):
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return [v for k, v in all_extensions.items() if not k in denylist]

# Core extensions needed to run Envoy's integration tests.
_core_extensions = [
    "envoy.access_loggers.file",
    "envoy.filters.http.router",
    "envoy.filters.http.health_check",
    "envoy.filters.network.http_connection_manager",
    "envoy.stat_sinks.statsd",
    "envoy.transport_sockets.raw_buffer",
]

# Return all core extensions to be compiled into Envoy.
def envoy_all_core_extensions():
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return [v for k, v in all_extensions.items() if k in _core_extensions]
