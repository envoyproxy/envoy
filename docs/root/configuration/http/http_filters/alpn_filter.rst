.. _config_http_filters_alpn:

ALPN
====

ALPN filter is an HTTP filter which enables Envoy to set ALPN in the TLS context used by upstream 
connections. :ref:`alpn_override <envoy_api_field_config.filter.http.alpn.v2alpha.FilterConfig.alpn_override>` provides 
the map from downstream protocol to the ALPN used by upstream connections.


Configuration
-------------

A sample filter configuration could be:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.alpn
    config:
      alpn_override:
      - downstream_protocol: HTTP10
        alpn: ["foo", "bar"]
      - downstream_protocol: HTTP11
        alpn: ["baz"]
      - downstream_protocol: HTTP2
        alpn: ["qux"]
  - name: envoy.router
    config: {}

.. attention::

  Make sure upstream protocol is the same as downstream protocol. Considering setting :ref:`USE_DOWNSTREAM_PROTOCOL <envoy_api_enum_value_Cluster.ClusterProtocolSelection.USE_DOWNSTREAM_PROTOCOL>`