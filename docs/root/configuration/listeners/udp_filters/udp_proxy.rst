.. _config_udp_listener_filters_udp_proxy:

UDP proxy
=========

.. attention::

  UDP proxy support should be considered alpha and not production ready.

* :ref:`v2 API reference <envoy_api_msg_config.filter.udp.udp_proxy.v2alpha.UdpProxyConfig>`
* This filter should be configured with the name *envoy.filters.udp_listener.udp_proxy*

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
<envoy_api_field_config.filter.udp.udp_proxy.v2alpha.UdpProxyConfig.idle_timeout>` is reached.

Load balancing and unhealthy host handling
------------------------------------------

Envoy will fully utilize the configured load balancer for the configured upstream cluster when
load balancing UDP datagrams. When a new session is created, Envoy will associate the session
with an upstream host selected using the configured load balancer. All future datagrams that
belong to the session will be routed to the same upstream host.

When an upstream host becomes unhealthy (due to :ref:`active health checking
<arch_overview_health_checking>`), Envoy will attempt to create a new session to a healthy host
when the next datagram is received.

Circuit breaking
----------------

The number of sessions that can be created per upstream cluster is limited by the cluster's
:ref:`maximum connection circuit breaker <arch_overview_circuit_break_cluster_maximum_connections>`.
By default this is 1024.

Example configuration
---------------------

The following example configuration will cause Envoy to listen on UDP port 1234 and proxy to a UDP
server listening on port 1235.

  .. code-block:: yaml

    admin:
      access_log_path: /tmp/admin_access.log
      address:
        socket_address:
          protocol: TCP
          address: 127.0.0.1
          port_value: 9901
    static_resources:
      listeners:
      - name: listener_0
        address:
          socket_address:
            protocol: UDP
            address: 127.0.0.1
            port_value: 1234
        listener_filters:
          name: envoy.filters.udp_listener.udp_proxy
          typed_config:
            '@type': type.googleapis.com/envoy.config.filter.udp.udp_proxy.v2alpha.UdpProxyConfig
            stat_prefix: service
            cluster: service_udp
      clusters:
      - name: service_udp
        connect_timeout: 0.25s
        type: STATIC
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: service_udp
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 1235

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
  downstream_sess_tx_errors, counter, Number of datagram transmission errors
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
  sess_rx_errors, Counter, Number of datagram receive errors
  sess_tx_datagrams, Counter, Number of datagrams transmitted
  sess_tx_errors, Counter, Number of datagrams tramsitted
