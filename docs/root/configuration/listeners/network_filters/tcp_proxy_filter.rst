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

Delayed upstream connection establishment
------------------------------------------

By default, the TCP proxy filter establishes the upstream connection immediately when a downstream connection is accepted.
However, in some scenarios it is beneficial to delay upstream connection establishment until certain conditions are met,
such as:

* Inspecting initial downstream data. For example, extracting SNI from TLS ``ClientHello``.
* Waiting for the downstream TLS handshake to complete to access client certificate information.
* Using the negotiated TLS parameters for routing decisions.

There are two ways to configure delayed upstream connection establishment:

Explicit configuration
^^^^^^^^^^^^^^^^^^^^^^

The preferred method is to use the :ref:`upstream_connect_trigger
<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.upstream_connect_trigger>`
configuration field. This provides explicit control over when the upstream connection is established.

The following modes are supported:

* ``IMMEDIATE`` (Default): Establish the upstream connection immediately when the downstream connection is accepted.
  This provides the lowest latency and is the default behavior for backward compatibility.
* ``ON_DOWNSTREAM_DATA``: Wait for initial data from the downstream connection before establishing the upstream
  connection. This allows preceding filters to inspect the initial data before the upstream connection is established.
  The filter buffers downstream data until the upstream connection is ready.
* ``ON_DOWNSTREAM_TLS_HANDSHAKE``: Wait for the downstream TLS handshake to complete before establishing the upstream
  connection. This allows access to the full TLS connection information, including client certificates and negotiated
  parameters. This mode is only effective when the downstream connection uses TLS. For non-TLS connections, it behaves
  the same as ``IMMEDIATE``.
* ``ON_DOWNSTREAM_DATA_AND_TLS_HANDSHAKE``: Wait for both initial data and TLS handshake completion before establishing
  the upstream connection. This provides maximum information about the downstream connection before connecting upstream.
  For non-TLS connections, this mode behaves the same as ``ON_DOWNSTREAM_DATA``.

Additional configuration options:

* ``max_wait_time``: Maximum time to wait for the trigger condition before forcing the upstream connection. This prevents
  indefinite waiting if the trigger condition is never met.
* ``downstream_data_config``: Configuration specific to data-triggered modes i.e., ``ON_DOWNSTREAM_DATA`` and
  ``ON_DOWNSTREAM_DATA_AND_TLS_HANDSHAKE``.

Example configuration:

.. code-block:: yaml

  name: envoy.filters.network.tcp_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
    stat_prefix: tcp
    cluster: upstream_cluster
    upstream_connect_trigger:
      mode: ON_DOWNSTREAM_DATA
      max_wait_time: 5s
      downstream_data_config:
        max_buffered_bytes: 8192

.. attention::

  Data-triggered modes (``ON_DOWNSTREAM_DATA`` and ``ON_DOWNSTREAM_DATA_AND_TLS_HANDSHAKE``) are not suitable for
  server-first protocols where the server sends the initial greeting (e.g., SMTP, MySQL, POP3). For such protocols,
  use ``IMMEDIATE`` mode or the connection will wait until timeout.

Filter state configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^

The legacy method using filter state is still supported for backward compatibility but is not recommended for new
deployments. This can be done by setting the ``StreamInfo`` filter state object for the key
``envoy.tcp_proxy.receive_before_connect`` to ``true``. Setting this filter state must happen in the
``initializeReadFilterCallbacks()`` callback of the network filter so that it is done before the TCP proxy filter
is initialized.

When the ``envoy.tcp_proxy.receive_before_connect`` filter state is set, the TCP proxy filter receives data before
the upstream connection has been established. In such a case, the TCP proxy filter buffers data it receives before
the upstream connection has been established and flushes it once the upstream connection is established. Filters can
also delay the upstream connection setup by returning ``StopIteration`` from their ``onNewConnection`` and ``onData``
callbacks. On receiving early data, the TCP proxy will read disable the connection until the upstream connection is
established. This is to protect the early buffer from overflowing.

.. note::

  When using the explicit configuration method (``upstream_connect_trigger``), the filter state approach
  is ignored. The two methods are mutually exclusive, with the explicit configuration taking precedence.

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
  early_data_received_count_total, Counter, Total number of connections where tcp proxy received data before upstream connection establishment is complete
  idle_timeout, Counter, Total number of connections closed due to idle timeout
  max_downstream_connection_duration, Counter, Total number of connections closed due to max_downstream_connection_duration timeout
  on_demand_cluster_attempt, Counter, Total number of connections that requested on demand cluster
  on_demand_cluster_missing, Counter, Total number of connections closed due to on demand cluster is missing
  on_demand_cluster_success, Counter, Total number of connections that requested and received on demand cluster
  on_demand_cluster_timeout, Counter, Total number of connections closed due to on demand cluster lookup timeout
  upstream_flush_total, Counter, Total number of connections that continued to flush upstream data after the downstream connection was closed
  upstream_flush_active, Gauge, Total connections currently continuing to flush upstream data after the downstream connection was closed
