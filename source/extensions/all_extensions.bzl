load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# Return all extensions to be compiled into Envoy.
def envoy_all_extensions():
  # These extensions are registered using the extension system but are required for the core
  # Envoy build.
  all_extensions = [
    "//source/extensions/transport_sockets/raw_buffer:config",
    "//source/extensions/transport_sockets/ssl:config",
  ]

  # These extensions can be removed on a site specific basis.
  for path in EXTENSIONS.values():
    all_extensions.append(path)

  return all_extensions
