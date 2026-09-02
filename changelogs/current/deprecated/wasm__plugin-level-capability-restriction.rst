The :ref:`PluginConfig.capability_restriction_config
<envoy_v3_api_field_extensions.wasm.v3.PluginConfig.capability_restriction_config>` field is
deprecated in favor of the new :ref:`VmConfig.capability_restriction_config
<envoy_v3_api_field_extensions.wasm.v3.VmConfig.capability_restriction_config>` field. The
restrictions are applied when the Wasm VM is created and are shared by every plugin running in that
VM, so they are a property of the VM rather than of an individual plugin. The deprecated field keeps
working: when it is set and the VM level field is not, it is used to populate the VM level one.
