The identity of a Wasm plugin (which plugin configurations share a single root context and
thread-local plugin instance inside a Wasm VM) is now derived from the whole
:ref:`plugin configuration <envoy_v3_api_msg_extensions.wasm.v3.PluginConfig>` instead of from the
plugin name and the traffic direction of the listener the plugin was configured on. Configurations
that differ in any field other than :ref:`vm_config
<envoy_v3_api_field_extensions.wasm.v3.PluginConfig.vm_config>` no longer share an instance, and
identical configurations now share one regardless of the traffic direction they are configured on.
