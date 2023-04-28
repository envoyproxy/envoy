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

.. literalinclude:: _include/wasm-network-filter.yaml
    :language: yaml
    :lines: 13-29
    :emphasize-lines: 3-12
    :linenos:
    :lineno-start: 13
    :caption: :download:`wasm-network-filter.yaml <_include/wasm-network-filter.yaml>`

The preceding snippet configures a filter from a Wasm binary on local disk.
