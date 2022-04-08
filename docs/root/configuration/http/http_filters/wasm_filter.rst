.. _config_http_filters_wasm:

Wasm
====

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.wasm.v3.Wasm>`

.. attention::

  The Wasm filter is experimental and is currently under active development. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The HTTP Wasm filter is used to implement an HTTP filter with a Wasm plugin.

.. note::

 This filter is not supported on Windows.

Example configuration
---------------------

Example filter configuration:

.. literalinclude:: ../../../start/sandboxes/_include/wasm-cc/envoy.yaml
    :language: yaml
    :lines: 24-49
    :emphasize-lines: 4-21
    :linenos:
    :lineno-start: 24
    :caption: :download:`wasm envoy.yaml <../../../start/sandboxes/_include/wasm-cc/envoy.yaml>`

The preceding snippet configures a filter from a Wasm binary on local disk.
