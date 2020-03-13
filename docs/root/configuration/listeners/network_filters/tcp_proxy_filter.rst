.. _config_network_filters_tcp_proxy:

TCP proxy
=========

* TCP proxy :ref:`architecture overview <arch_overview_tcp_proxy>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.network.tcp_proxy.v2.TcpProxy>`
* This filter should be configured with the name *envoy.filters.network.tcp_proxy*.

.. _config_network_filters_tcp_proxy_dynamic_cluster:

Dynamic cluster selection
-------------------------

The upstream cluster used by the TCP proxy filter can be dynamically set by
other network filters on a per-connection basis by setting a per-connection
state object under the key `envoy.tcp_proxy.cluster`. See the
implementation for the details.

.. _config_network_filters_tcp_proxy_subset_lb:

Routing to a subset of hosts
----------------------------

TCP proxy can be configured to route to a subset of hosts within an upstream cluster.

To define metadata that a suitable upstream host must match, use one of the following fields:

#. Use :ref:`TcpProxy.metadata_match<envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.metadata_match>`
   to define required metadata for a single upstream cluster.
#. Use :ref:`ClusterWeight.metadata_match<envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>`
   to define required metadata for a weighted upstream cluster.
#. Use combination of :ref:`TcpProxy.metadata_match<envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.metadata_match>`
   and :ref:`ClusterWeight.metadata_match<envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>`
   to define required metadata for a weighted upstream cluster (metadata from the latter will be merged on top of the former).

.. _config_network_filters_tcp_proxy_stats:

Statistics
----------

The TCP proxy filter emits both its own downstream statistics as well as many of the :ref:`cluster
upstream statistics <config_cluster_manager_cluster_stats>` where applicable. The downstream
statistics are rooted at *tcp.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_cx_total, Counter, Total number of connections handled by the filter
  downstream_cx_no_route, Counter, Number of connections for which no matching route was found or the cluster for the route was not found
  downstream_cx_tx_bytes_total, Counter, Total bytes written to the downstream connection
  downstream_cx_tx_bytes_buffered, Gauge, Total bytes currently buffered to the downstream connection
  downstream_cx_rx_bytes_total, Counter, Total bytes read from the downstream connection
  downstream_cx_rx_bytes_buffered, Gauge, Total bytes currently buffered from the downstream connection
  downstream_flow_control_paused_reading_total, Counter, Total number of times flow control paused reading from downstream
  downstream_flow_control_resumed_reading_total, Counter, Total number of times flow control resumed reading from downstream
  idle_timeout, Counter, Total number of connections closed due to idle timeout
  upstream_flush_total, Counter, Total number of connections that continued to flush upstream data after the downstream connection was closed
  upstream_flush_active, Gauge, Total connections currently continuing to flush upstream data after the downstream connection was closed
