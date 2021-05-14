load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# Return the extension cc_library target after select
def _selected_extension_target(target):
    return target + "_envoy_extension"

def _get_extensions():
    return {
        k: v
        for k, v in EXTENSIONS.items()
        if v.get("required") or
           not v.get("builtin")
    }

# Return all extensions to be compiled into Envoy.
def envoy_all_extensions(denylist = []):
    # These extensions can be removed on a site specific basis.
    return [_selected_extension_target(v["source"]) for k, v in _get_extensions().items() if not k in denylist]

# Return all core extensions to be compiled into Envoy.
def envoy_all_core_extensions():
    # These extensions can be removed on a site specific basis.
    return [v["source"] for k, v in _get_extensions().items() if v.get("core")]

_http_filter_prefix = "envoy.filters.http"

def envoy_all_http_filters():
    return [_selected_extension_target(v["source"]) for k, v in _get_extensions().items() if k.startswith(_http_filter_prefix)]

# All network-layer filters are extensions with names that have the following prefix.
_network_filter_prefix = "envoy.filters.network"

# All thrift filters are extensions with names that have the following prefix.
_thrift_filter_prefix = "envoy.filters.thrift"

# Return all network-layer filter extensions to be compiled into network-layer filter generic fuzzer.
def envoy_all_network_filters():
    return [_selected_extension_target(v["source"]) for k, v in _get_extensions().items() if (k.startswith(_network_filter_prefix) or k.startswith(_thrift_filter_prefix))]
