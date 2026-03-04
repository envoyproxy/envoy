.. _config_http_filters_a2a:

A2A
===

The A2A filter provides Agent-to-Agent protocol support for Envoy.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
