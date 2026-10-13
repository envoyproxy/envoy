Fixed a bug where Wasm plugins whose :ref:`VM configurations
<envoy_v3_api_msg_extensions.wasm.v3.VmConfig>` differed could still share a single Wasm VM, and so
silently run with the VM configuration of whichever plugin happened to be configured first. The
:ref:`runtime <envoy_v3_api_field_extensions.wasm.v3.VmConfig.runtime>` and the :ref:`capability
restrictions <envoy_v3_api_field_extensions.wasm.v3.VmConfig.capability_restriction_config>` are now
part of the VM identity, alongside the ``vm_id``, the ``configuration``, the ``code`` and the
``environment_variables``, so plugins differing in either of them no longer share a VM.
