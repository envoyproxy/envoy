.. _config_network_filters_sni_cluster:

Upstream Cluster from SNI
=========================

The `sni_cluster` is a network filter that uses the SNI value in a TLS
connection as the upstream cluster name. The filter will not modify the
upstream cluster for non-TLS connections.

This filter has no configuration. It must be installed before the
:ref:`tcp_proxy <config_network_filters_tcp_proxy>` filter.

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`
