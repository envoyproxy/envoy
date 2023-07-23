.. _config_wasm_service:

Wasm service
============

The :ref:`WasmService <envoy_v3_api_msg_extensions.wasm.v3.WasmService>` configuration specifies a
singleton or per-worker Wasm service for background or on-demand activities.

Example plugin configuration:

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.bootstrap.wasm
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.wasm.v3.WasmService
      singleton: true
      config:
        name: "my_plugin"
        configuration:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: |
            {
              "my_config_value": "my_value"
            }
        vm_config:
          code:
            local:
              filename: "/etc/envoy_filter_http_wasm_example.wasm"

The preceding snippet configures a plugin singleton service from a Wasm binary on local disk.
