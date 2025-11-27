.. _config_http_filters_connection_pool_cardinality:

Connection Pool Cardinality
===

This filter allows envoy to create additional connection pools all the upstreams that downstream
requests that flow through this filter goes through. For example using it in the ingress http
connection manager that connects to the local application when running Envoy as a sidecar.

To tune this monitor the connections active with the clusters with which you want to bump the
number of connection pools up to and select a number that magnifies the number of connections
without adding significant memory burden. For example in the ingress sidecar scenario, by default
we'd have a minimum of `|| num_workers ||` connections to the local application, but we can
bump it to a minimum of `|| num_workers * connection_pool_cardinality ||` with this filter. So if
we have 4 envoy workers and a connection pool cardinality of 10, we'd go from a minimum of
4 connection -> 40 connections between the envoy and the local application for ingress.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.connection_pool_cardinality
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.connection_pool_cardinality.v3.ConnectionPoolCardinalityConfig
      connection_pool_count: 10

