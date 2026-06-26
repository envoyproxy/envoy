.. _config_http_filters_ai_protocol_manager:

AI Protocol Manager
===================

The AI Protocol Manager filter is currently a no-op pass-through filter with an
empty configuration.

This filter should be configured with the type URL
``type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager``.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
