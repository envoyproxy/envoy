.. _well_known_filter_state:

Well Known Filter State Objects
===============================

The following lists the filter state object keys used by the Envoy extensions to programmatically modify their behavior:

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

``envoy.tcp_proxy.per_connection_idle_timeout_ms``
  :ref:`TCP proxy idle timeout duration
  <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.idle_timeout>` override on a per-connection
  basis. Accepts a count of milliseconds number string as a constructor.

``envoy.ratelimit.hits_addend``
  :ref:`Rate Limit Hits Addend
  <envoy_v3_api_field_service.ratelimit.v3.RateLimitRequest.hits_addend>` override on a per-route basis.
  Accepts a number string as a constructor.

``envoy.geoip``
  :ref:`Network GeoIP filter <config_network_filters_geoip>` stores geolocation lookup results
  in this filter state object. The object contains fields for geographic data such as country,
  city, region, and ASN. Supports serialization for access logging and field-level access. Fields:

  * ``country``: ISO country code;
  * ``city``: city name;
  * ``region``: ISO region code;
  * ``asn``: autonomous system number;
  * ``anon``: anonymization network check result (``true`` or ``false``);
  * ``anon_vpn``: VPN check result (``true`` or ``false``);
  * ``anon_hosting``: hosting provider check result (``true`` or ``false``);
  * ``anon_tor``: TOR exit node check result (``true`` or ``false``);
  * ``anon_proxy``: public proxy check result (``true`` or ``false``);
  * ``isp``: ISP name;
  * ``apple_private_relay``: iCloud Private Relay check result (``true`` or ``false``).

``envoy.filters.http.mcp.request``
  :ref:`MCP filter <config_http_filters_mcp>` stores parsed MCP (Model Context Protocol) JSON-RPC
  request attributes when ``emit_filter_state`` is enabled. The object stores extracted fields
  from the parsed request.

``envoy.network.network_namespace``
  Contains the value of the downstream connection's Linux network namespace if it differs from the default.

``envoy.network.upstream_bind_override.network_namespace``
  Allows overriding the network namespace on the upstream connections using the :ref:`Linux network
  namespace local address selector
  <envoy_v3_api_msg_extensions.local_address_selectors.filter_state_override.v3.Config>`
  extension. The object should serialize to the network namespace filepath, and the empty string
  value clears the network namespace. This object is expected to be shared from the downstream
  filters with the upstream connections.

``envoy.tls.certificate_mappers.on_demand_secret``
  Allows overriding the certificate to use per-connection using the :ref:`filter state certificate mapper
  <envoy_v3_api_msg_extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config>`.

Filter state object factories
-----------------------------

The following generic filter state factories can be used to create filter state objects via
configuration with a :ref:`factory lookup key
<envoy_v3_api_field_extensions.filters.common.set_filter_state.v3.FilterStateValue.factory_key>`.

``envoy.string``
  A generic string object factory for creating filter state entries with custom key names.
  Use this as the :ref:`factory_key
  <envoy_v3_api_field_extensions.filters.common.set_filter_state.v3.FilterStateValue.factory_key>`
  when your ``object_key`` is a custom name not listed in this document.

  Example configuration:

  .. code-block:: yaml

    object_key: my.custom.key
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "my-value"

  This creates a filter state entry named ``my.custom.key`` containing the string ``my-value``.
  The value can be accessed in access logs using ``%FILTER_STATE(my.custom.key)%``.

``envoy.network.ip``
  A factory to create IP addresses from ``IPv4`` and ``IPv6`` address strings.


Filter state object fields
--------------------------

The filter state object fields can be used in the format strings. For example,
the following format string references the port number in the original
destination cluster filter state object:

.. code-block:: none

  %FILTER_STATE(envoy.network.transport_socket.original_dst_address:FIELD:port)%
