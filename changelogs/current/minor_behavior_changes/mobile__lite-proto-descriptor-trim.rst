Envoy Mobile (lite-proto builds) now only embeds the protobuf descriptors it can reflect: the
bootstrap, the route match actions, and the config protos of the extensions in
``mobile/envoy_build_config/extension_registry.cc``. Admin, xDS service and mobile listener
descriptors are only embedded when that functionality is enabled. This removes about 100 unused
descriptors, about 144 KiB of native code and data in an arm64 Android build. Applications that
register extra extensions in a lite-proto build must load their descriptors with
``Envoy::loadFileDescriptors()``, as Envoy Mobile already does for its own extensions.
