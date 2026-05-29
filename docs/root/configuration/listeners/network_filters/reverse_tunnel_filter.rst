.. _config_network_filters_reverse_tunnel:

Reverse tunnel
==============

The reverse tunnel network filter accepts or rejects reverse connection requests by parsing
HTTP/1.1 requests with Node ID, Cluster ID, and Tenant ID headers and optionally validating these
values using the Envoy filter state.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.reverse_tunnel.v3.ReverseTunnel>`

Configuration notes:

- **HTTP method**: ``request_method`` uses :ref:`RequestMethod <envoy_v3_api_enum_config.core.v3.RequestMethod>`. If not specified, it defaults to ``GET``.
- In this version, the filter does not perform additional request validation against filter state or metadata.
- **Tenant isolation**: ``enable_tenant_isolation`` scopes cached reverse connections by tenant.
  When enabled, the filter derives composite identifiers of the form ``<tenant>:<node>`` and
  ``<tenant>:<cluster>`` so that the same node or cluster identifier can be reused across tenants.
  To avoid ambiguity, handshake requests that contain the ``:`` delimiter in any of the reverse
  tunnel headers are rejected. This option is disabled by default for backwards compatibility.
