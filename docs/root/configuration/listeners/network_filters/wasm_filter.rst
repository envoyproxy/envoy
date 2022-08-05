.. _config_network_filters_wasm:

Wasm Network Filter
===================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.wasm.v3.Wasm>`

.. attention::

  The Wasm filter is experimental and is currently under active development. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The Wasm network filter is used to implement a network filter with a Wasm plugin.


Example configuration
---------------------

Example filter configuration:

.. literalinclude:: _include/wasm-filter-proxy.yaml
    :language: yaml
    :linenos:
    :caption: :download:`wasm-filter-proxy.yaml <_include/wasm-filter-proxy.yaml>`


The preceding snippet configures a filter from a Wasm binary on local disk.
