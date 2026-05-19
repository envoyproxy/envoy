Fixed a bug where Envoy did not recreate the Wasm VM when only
:ref:`environment_variables <envoy_v3_api_field_extensions.wasm.v3.VmConfig.environment_variables>`
changed in ``vm_config``. The VM was previously reused from the cache because environment variables
were not included in the vm_key computation.
