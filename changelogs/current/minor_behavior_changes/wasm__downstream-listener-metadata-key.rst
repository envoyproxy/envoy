Downstream HTTP and network Wasm filters no longer rely on the listener
metadata as part of the plugin unique key. That means that if two listeners
share the same Wasm filter configuration, but have distinct metadata, they will
now internally use the same instance of the Wasm plugin. To force distinct
runtime instances of the Wasm plugins, please assign distinct :ref:`names
<envoy_v3_api_field_extensions.wasm.v3.PluginConfig.name>` or root/VM ID to
each HTTP or network Wasm filter configuration.
