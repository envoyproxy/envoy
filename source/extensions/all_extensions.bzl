load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS", "WINDOWS_EXTENSIONS")

# These extensions are registered using the extension system but are required for the core Envoy build.
# The map may be overridden by extensions specified in envoy_build_config.
_required_extensions = {
    "envoy.common.crypto.utility_lib": "//source/extensions/common/crypto:utility_lib",
    "envoy.transport_sockets.tls": "//source/extensions/transport_sockets/tls:config",
}

# Return all extensions to be compiled into Envoy.
def envoy_all_extensions(blacklist = dict()):
    all_extensions = dicts.add(_required_extensions, EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return [v for k, v in all_extensions.items() if not k in blacklist.values()]

def envoy_windows_extensions():
    all_extensions = dicts.add(_required_extensions, WINDOWS_EXTENSIONS)

    # These extensions can be removed on a site specific basis.
    return all_extensions.values()
