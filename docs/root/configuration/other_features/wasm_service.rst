.. _config_wasm_service:

Wasm service
============

The :ref:`WasmService <envoy_v3_api_msg_extensions.wasm.v3.WasmService>` configuration specifies a
singleton or per-worker Wasm service for background or on-demand activities.

Example plugin configuration:

.. code-block:: yaml

  wasm:
    config:
      config:
        name: "my_plugin"
        vm_config:
          runtime: "envoy.wasm.runtime.v8"
          code:
            local:
              filename: "/etc/envoy_filter_http_wasm_example.wasm"
      singleton: true

The preceding snippet configures a plugin singleton service from a Wasm binary on local disk.
