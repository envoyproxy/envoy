.. _config_http_filters_wasm:

Wasm
====

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.wasm.v3.Wasm>`

.. attention::

  The Wasm filter is experimental and is currently under active development. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The HTTP Wasm filter is used implement an HTTP filter with a Wasm plugin.

Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.wasm
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm
    config:
      config:
        name: "my_plugin"
        vm_config:
          runtime: "envoy.wasm.runtime.v8"
          code:
            local:
              filename: "/etc/envoy_filter_http_wasm_example.wasm"
          allow_precompiled: true
 

The preceding snippet configures a filter from a Wasm binary on local disk.
