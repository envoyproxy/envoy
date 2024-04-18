.. _config_udp_listener_filters_udp_proxy:

UDP proxy
=========

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>`

Overview
--------

The UDP proxy listener filter allows Envoy to operate as a *non-transparent* proxy between a
UDP client and server. The lack of transparency means that the upstream server will see the
source IP and port of the Envoy instance versus the client. All datagrams flow from the client, to
Envoy, to the upstream server, back to Envoy, and back to the client.

Because UDP is not a connection oriented protocol, Envoy must keep track of a client's *session*
such that the response datagrams from an upstream server can be routed back to the correct client.
Each session is index by the 4-tuple consisting of source IP/port and local IP/port that the
datagram is received on. Sessions last until the :ref:`idle timeout
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.idle_timeout>` is reached.

Above *session stickness* could be disabled by setting :ref:`use_per_packet_load_balancing
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>` to true.
In that case, *per packet load balancing* is enabled. It means that upstream host is selected on every single data chunk
received by udp proxy using currently used load balancing policy.

The UDP proxy listener filter also can operate as a *transparent* proxy if the
:ref:`use_original_src_ip <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>`
field is set to true. But please keep in mind that it does not forward the port to upstreams. It forwards only the IP address to upstreams.

Load balancing and unhealthy host handling
------------------------------------------

Envoy will fully utilize the configured load balancer for the configured upstream cluster when
load balancing UDP datagrams. By default, when a new session is created, Envoy will associate the session
with an upstream host selected using the configured load balancer. All future datagrams that
belong to the session will be routed to the same upstream host. However, if :ref:`use_per_packet_load_balancing
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>`
field is set to true, Envoy selects another upstream host on next datagram using the configured load balancer
and creates a new session if such does not exist. So in case of several upstream hosts available for the load balancer
each data chunk is forwarded to a different host.

When an upstream host becomes unhealthy (due to :ref:`active health checking
<arch_overview_health_checking>`), Envoy will attempt to create a new session to a healthy host
when the next datagram is received.

Circuit breaking
----------------

The number of sessions that can be created per upstream cluster is limited by the cluster's
:ref:`maximum connection circuit breaker <arch_overview_circuit_break_cluster_maximum_connections>`.
By default this is 1024.


.. _config_udp_listener_filters_udp_proxy_routing:

Routing
-------

The filter can route different datagrams to different upstream clusters with their source
addresses. The matching API can be used with UDP routing, by specifying a matcher, a
:ref:`UDP network input <extension_category_envoy.matching.network.input>` as the matching rule and
:ref:`Route <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.Route>` as the resulting action.

The following matcher configuration will lead Envoy to route UDP datagrams according to their
source IPs, ignore datagrams other than those with a source IP of 127.0.0.1, and then filter the
remaining datagrams to different clusters according to their source ports.

.. literalinclude:: _include/udp-proxy-router.yaml
   :language: yaml
   :linenos:
   :lineno-start: 14
   :lines: 14-53
   :caption: :download:`udp-proxy-router.yaml <_include/udp-proxy-router.yaml>`

.. _config_udp_listener_filters_udp_proxy_session_filters:

Session filters
---------------

The UDP proxy is able to apply :ref:`session filters <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.session_filters>`.
These kinds of filters run seprately on each upstream UDP session and support a more granular API that allows running operations only
at the start of an upstream UDP session, when a UDP datagram is received from the downstream and when a UDP datagram is received from the
upstream, similar to network filters.

.. note::
  When using session filters, choosing the upstream host only happens after completing the ``onNewSession()`` iteration for all
  the filters in the chain. This allows choosing the host based on decisions made in one of the session filters, and prevents the
  creation of upstream sockets in cases where one of the filters stopped the filter chain.
  Additionally, since :ref:`per packet load balancing <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>` require
  choosing the upstream host for each received datagram, session filters can't be used when this option is enabled.

Envoy has the following builtin UDP session filters.

.. toctree::
  :maxdepth: 2

  session_filters/http_capsule
  session_filters/dynamic_forward_proxy

.. _config_udp_listener_filters_udp_proxy_tunneling_over_http:

UDP Tunneling over HTTP
-----------------------

The UDP proxy filter can be used to tunnel raw UDP over HTTP requests. Refer to :ref:`HTTP upgrades <tunneling-udp-over-http>` for more information.

UDP tunneling configuration can be used by setting :ref:`tunneling_config <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.tunneling_config>`

.. note::
  Since :ref:`per packet load balancing <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>` require
  choosing the upstream host for each received datagram, tunneling can't be used when this option is enabled.

Example configuration
---------------------

The following example configuration will cause Envoy to listen on UDP port 1234 and proxy to a UDP
server listening on port 1235, allowing 9000 byte packets in both directions (i.e., either jumbo
frames or fragmented IP packets).

.. literalinclude:: _include/udp-proxy.yaml
   :language: yaml
   :linenos:
   :caption: :download:`udp-proxy.yaml <_include/udp-proxy.yaml>`

.. _config_udp_listener_filters_udp_proxy_stats:

Statistics
----------

The UDP proxy filter emits both its own downstream statistics as well as many of the :ref:`cluster
upstream statistics <config_cluster_manager_cluster_stats>` where applicable. The downstream
statistics are rooted at *udp.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_sess_no_route, Counter, Number of datagrams not routed due to no cluster
  downstream_sess_rx_bytes, Counter, Number of bytes received
  downstream_sess_rx_datagrams, Counter, Number of datagrams received
  downstream_sess_rx_errors, Counter, Number of datagram receive errors
  downstream_sess_total, Counter, Number sessions created in total
  downstream_sess_tx_bytes, Counter, Number of bytes transmitted
  downstream_sess_tx_datagrams, Counter, Number of datagrams transmitted
  downstream_sess_tx_errors, Counter, Number of datagram transmission errors
  idle_timeout, Counter, Number of sessions destroyed due to idle timeout
  downstream_sess_active, Gauge, Number of sessions currently active

The following standard :ref:`upstream cluster stats <config_cluster_manager_cluster_stats>` are used
by the UDP proxy:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_cx_none_healthy, Counter, Number of datagrams dropped due to no healthy hosts
  upstream_cx_overflow, Counter, Number of datagrams dropped due to hitting the session circuit breaker
  upstream_cx_rx_bytes_total, Counter, Number of bytes received
  upstream_cx_tx_bytes_total, Counter, Number of bytes transmitted

The UDP proxy filter also emits custom upstream cluster stats prefixed with
*cluster.<cluster_name>.udp.*:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  sess_rx_datagrams, Counter, Number of datagrams received
  sess_rx_datagrams_dropped, Counter, Number of datagrams dropped due to kernel overflow or truncation
  sess_rx_errors, Counter, Number of datagram receive errors
  sess_tx_datagrams, Counter, Number of datagrams transmitted
  sess_tx_errors, Counter, Number of datagrams transmitted
  sess_tunnel_success, Counter, Number of successfully established UDP tunnels
  sess_tunnel_failure, Counter, Number of UDP tunnels failed to establish
  sess_tunnel_buffer_overflow, Counter, Number of datagrams dropped due to full tunnel buffer
