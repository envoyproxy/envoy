load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# These extensions are registered using the extension system but are required for the core Envoy build.
# The map may be overridden by extensions specified in envoy_build_config.
_required_extensions = {
    "envoy.http.original_ip_detection.xff": "//source/extensions/http/original_ip_detection/xff:config",
    "envoy.request_id.uuid": "//source/extensions/request_id/uuid:config",
    "envoy.transport_sockets.tls": "//source/extensions/transport_sockets/tls:config",
    "envoy.network.dns_resolver.cares": "//source/extensions/network/dns_resolver/cares:config",
    "envoy.network.dns_resolver.apple": "//source/extensions/network/dns_resolver/apple:config",
}

# Return the extension cc_library target after select
def _selected_extension_target(target):
    return target + "_envoy_extension"

# Return all extensions to be compiled into Envoy.
def envoy_all_extensions(denylist = []):
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return [_selected_extension_target(v) for k, v in all_extensions.items() if not k in denylist]

# Core extensions needed to run Envoy's integration tests.
_core_extensions = [
    "envoy.access_loggers.file",
    "envoy.access_loggers.stream",
    "envoy.filters.http.router",
    "envoy.filters.http.health_check",
    "envoy.filters.network.http_connection_manager",
    "envoy.stat_sinks.statsd",
    "envoy.transport_sockets.raw_buffer",
    "envoy.network.dns_resolver.cares",
    "envoy.network.dns_resolver.apple",
]

# Return all core extensions to be compiled into Envoy.
def envoy_all_core_extensions():
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return [v for k, v in all_extensions.items() if k in _core_extensions]

_http_filter_prefix = "envoy.filters.http"

def envoy_all_http_filters():
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    return [_selected_extension_target(v) for k, v in all_extensions.items() if k.startswith(_http_filter_prefix)]

# All network-layer filters are extensions with names that have the following prefix.
_network_filter_prefix = "envoy.filters.network"

# All thrift filters are extensions with names that have the following prefix.
_thrift_filter_prefix = "envoy.filters.thrift"

# Return all network-layer filter extensions to be compiled into network-layer filter generic fuzzer.
def envoy_all_network_filters():
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    return [_selected_extension_target(v) for k, v in all_extensions.items() if (k.startswith(_network_filter_prefix) or k.startswith(_thrift_filter_prefix))]
