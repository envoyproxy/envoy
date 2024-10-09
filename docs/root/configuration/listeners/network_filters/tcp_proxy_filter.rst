.. _config_network_filters_tcp_proxy:

TCP proxy
=========

* TCP proxy :ref:`architecture overview <arch_overview_tcp_proxy>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`

.. _config_network_filters_tcp_proxy_dynamic_cluster:

Dynamic cluster selection
-------------------------

The upstream cluster used by the TCP proxy filter can be dynamically set by
other network filters on a per-connection basis by setting a per-connection
state object under the key ``envoy.tcp_proxy.cluster``. See the
implementation for the details.

.. _config_network_filters_tcp_proxy_subset_lb:

Routing to a subset of hosts
----------------------------

TCP proxy can be configured to route to a subset of hosts within an upstream cluster.

To define metadata that a suitable upstream host must match, use one of the following fields:

#. Use :ref:`TcpProxy.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.metadata_match>`
   to define required metadata for a single upstream cluster.
#. Use :ref:`ClusterWeight.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>`
   to define required metadata for a weighted upstream cluster.
#. Use combination of :ref:`TcpProxy.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.metadata_match>`
   and :ref:`ClusterWeight.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>`
   to define required metadata for a weighted upstream cluster (metadata from the latter will be merged on top of the former).

In addition, dynamic metadata can be set by earlier network filters on the ``StreamInfo``. Setting the dynamic metadata
must happen before ``onNewConnection()`` is called on the ``TcpProxy`` filter to affect load balancing.

.. _config_network_filters_tcp_proxy_receive_before_connect:

Early reception and delayed upstream connection establishment
-------------------------------------------------------------

``TcpProxy`` filter  normally disables reading on the downstream connection until the upstream connection has been established. In some situations earlier filters in the filter chain may need to read data from the downstream connection before allowing the upstream connection to be established. This can be done by setting the ``StreamInfo`` filter state object for the key ``envoy.tcp_proxy.receive_before_connect``. Setting this filter state must happen in ``initializeReadFilterCallbacks()`` callback of the network filter so that is done before ``TcpProxy`` filter is initialized.

Network filters can also pass data upto the ``TcpProxy`` filter before the upstream connection has been established, as ``TcpProxy`` filter now buffers data it receives before the upstream connection has been established to be sent when the upstream connection is established. Filters can also delay the upstream connection setup by returning ``StopIteration`` from their ``onNewConnection`` and ``onData`` callbacks.
On receiving early data, TCP_PROXY will read disable the connection until the upstream connection is established. This is to prevent the connection from being closed by the peer if the upstream connection is not established in a timely manner.

.. _config_network_filters_tcp_proxy_tunneling_over_http:

Tunneling TCP over HTTP
-----------------------

The TCP proxy filter can be used to tunnel raw TCP over HTTP ``CONNECT`` or HTTP ``POST`` requests. Refer to :ref:`HTTP upgrades <tunneling-tcp-over-http>` for more information.

TCP tunneling configuration can be used by setting :ref:`Tunneling Config <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>`

Additionally, if tunneling was enabled for a TCP session by configuration, it can be dynamically disabled per connection,
by setting a per-connection filter state object under the key ``envoy.tcp_proxy.disable_tunneling``. Refer to the implementation for more details.

.. _config_network_filters_tcp_proxy_stats:

Statistics
----------

The TCP proxy filter emits both its own downstream statistics,
:ref:`access logs <config_access_log>` for upstream and downstream connections,
as well as many of the
:ref:`cluster upstream statistics <config_cluster_manager_cluster_stats>` where applicable.
The downstream statistics are rooted at *tcp.<stat_prefix>.* with the following statistics:

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
  max_downstream_connection_duration, Counter, Total number of connections closed due to max_downstream_connection_duration timeout
  on_demand_cluster_attempt, Counter, Total number of connections that requested on demand cluster
  on_demand_cluster_missing, Counter, Total number of connections closed due to on demand cluster is missing
  on_demand_cluster_success, Counter, Total number of connections that requested and received on demand cluster
  on_demand_cluster_timeout, Counter, Total number of connections closed due to on demand cluster lookup timeout
  upstream_flush_total, Counter, Total number of connections that continued to flush upstream data after the downstream connection was closed
  upstream_flush_active, Gauge, Total connections currently continuing to flush upstream data after the downstream connection was closed
