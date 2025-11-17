.. _well_known_filter_state:

Well Known Filter State Objects
===============================

The following lists the filter state object keys used by the Envoy extensions:

``envoy.network.upstream_server_name``
  Sets the transport socket option to override the `SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ in
  the upstream connections. Accepts a host name as a constructor, e.g. "lyft.com".

``envoy.network.application_protocols``
  Sets the transport socket option to override the `ALPN <https://en.wikipedia.org/wiki/Application-Layer Protocol
  Negotiation>`_ list in the upstream connections. This setting takes precedence over the upstream cluster configuration.
  Accepts a comma-separated list of protocols as a constructor, e.g. "h2,http/1.1".

``envoy.network.upstream_subject_alt_names``
  Enables additional verification of the upstream peer certificate SAN names. Accepts a comma-separated list of SAN
  names as a constructor.

``envoy.network.ip``
  Shared Filter State object used to create an IP address.
  Accepts both `IPv4`` and `IPv6` string as a constructor.

``envoy.tcp_proxy.cluster``
  :ref:`TCP proxy <config_network_filters_tcp_proxy>` dynamic cluster name selection on a per-connection basis. Accepts
  a cluster name as a constructor.

``envoy.udp_proxy.cluster``
  :ref:`UDP proxy <config_udp_listener_filters_udp_proxy>` dynamic cluster name selection on a per-session basis. Accepts
  a cluster name as a constructor.

``envoy.network.transport_socket.original_dst_address``
  :ref:`Original destination cluster <arch_overview_load_balancing_types_original_destination>` dynamic address
  selection. Accepts an `IP:PORT` string as a constructor. Fields:

  * ``ip``: IP address value as a string;
  * ``port``: port value as a number.

``envoy.filters.listener.original_dst.local_ip``
  :ref:`Original destination listener filter <config_listener_filters_original_dst>` destination address selection for
  the internal listeners. Accepts an `IP:PORT` string as a constructor. Fields:

  * ``ip``: IP address value as a string;
  * ``port``: port value as a number.

``envoy.filters.listener.original_dst.remote_ip``
  :ref:`Original destination listener filter <config_listener_filters_original_dst>` source address selection for the
  internal listeners. Accepts an `IP:PORT` string as a constructor. Fields:

  * ``ip``: IP address value as a string;
  * ``port``: port value as a number.

``envoy.upstream.dynamic_host``
  :ref:`Dynamic forward proxy <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>` upstream
  host override on a per-connection basis. Accepts a host string as a constructor.

``envoy.upstream.dynamic_port``
  :ref:`Dynamic forward proxy <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>` upstream
  port override on a per-connection basis. Accepts a port number string as a constructor.

``envoy.tcp_proxy.disable_tunneling``
  :ref:`TCP proxy tunneling override
  <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>` to disable tunneling on a
  per-connection bases. Accepts values "true" and "false".

``envoy.filters.network.http_connection_manager.local_reply_owner``
  Shared filter status for logging which filter config name in the HTTP filter chain sent the local reply.

``envoy.string``
  A special generic string object factory, to be used as a :ref:`factory lookup key
  <envoy_v3_api_field_extensions.filters.common.set_filter_state.v3.FilterStateValue.factory_key>`.

``envoy.tcp_proxy.per_connection_idle_timeout_ms``
  :ref:`TCP proxy idle timeout duration
  <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.idle_timeout>` override on a per-connection
  basis. Accepts a count of milliseconds number string as a constructor.

``envoy.ratelimit.hits_addend``
  :ref:`Rate Limit Hits Addend
  <envoy_v3_api_field_service.ratelimit.v3.RateLimitRequest.hits_addend>` override on a per-route basis.
  Accepts a number string as a constructor.

``envoy.network.network_namespace``
  Contains the value of the downstream connection's Linux network namespace if it differs from the default.

Filter state object fields
--------------------------

The filter state object fields can be used in the format strings. For example,
the following format string references the port number in the original
destination cluster filter state object:

.. code-block:: none

  %FILTER_STATE(envoy.network.transport_socket.original_dst_address:FIELD:port)%
