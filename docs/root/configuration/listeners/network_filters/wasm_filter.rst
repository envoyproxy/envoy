.. _config_network_filters_wasm:

Wasm Network Filter
===============================================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.rbac.v3.RBAC>`

.. attention::

  The Wasm filter is experimental and is currently under active development. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The Wasm network filter is used to implement a network filter with a Wasm plugin. 


Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.network.wasm
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.wasm.v3.Wasm
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
