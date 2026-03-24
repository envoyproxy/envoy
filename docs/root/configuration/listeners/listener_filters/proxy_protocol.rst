.. _config_listener_filters_proxy_protocol:

Proxy Protocol
==============

This listener filter adds support for
`HAProxy Proxy Protocol <https://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_.

In this mode, the downstream connection is assumed to come from a proxy
which places the original coordinates (IP, PORT) into a connection-string.
Envoy then extracts these and uses them as the remote address.

In Proxy Protocol v2 there exists the concept of extensions (TLV)
tags that are optional. If the type of the TLV is added to the filter's configuration,
the TLV will be emitted as dynamic metadata or filter state with user-specified key.

TLV Storage Options
-------------------

The filter supports two storage locations for TLV values, controlled by the
:ref:`tlv_location <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.tlv_location>` setting:

**DYNAMIC_METADATA** (default)
  TLV values are stored in dynamic metadata under the ``envoy.filters.listener.proxy_protocol`` namespace.
  This allows access via :ref:`DynamicMetadataInput <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.DynamicMetadataInput>`
  in RBAC and other matchers.

**FILTER_STATE**
  TLV values are stored in filter state as a single map-like object under the key
  ``envoy.network.proxy_protocol.tlv``. Individual TLV values can be accessed in two ways:

  1. Via CEL expressions: ``filter_state["envoy.network.proxy_protocol.tlv"]["my_key"]``

  2. Via :ref:`FilterStateInput <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.FilterStateInput>`
     with the ``field`` parameter, which enables direct field-level access in RBAC and other matchers
     without needing CEL expressions:

  .. code-block:: yaml

    listener_filters:
      - name: envoy.filters.listener.proxy_protocol
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol
          tlv_location: FILTER_STATE
          rules:
            - tlv_type: 0xEA
              on_tlv_present:
                key: "aws_vpce_id"

  With this configuration, you can match on individual TLV values directly in RBAC using
  the ``field`` parameter on ``FilterStateInput``:

  .. code-block:: yaml

    matcher:
      matcher_tree:
        input:
          name: filter_state
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
            key: "envoy.network.proxy_protocol.tlv"
            field: "aws_vpce_id"
        exact_match_map:
          map:
            "vpce-12345678":
              action:
                name: allow

TLV Value Format
----------------

TLV payloads in PROXY Protocol v2 are arbitrary binary data, not UTF-8 text. By default (``RAW_STRING``),
the filter sanitizes non-UTF-8 bytes by replacing them with ``0x21`` (``!``), which can silently corrupt
binary payloads. For binary TLV values, use the ``HEX_STRING`` format to get a lossless hex-encoded
representation.

The format is controlled per-rule via the
:ref:`value_format <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.KeyValuePair.value_format>` field:

**RAW_STRING** (default)
  The TLV value is stored as a sanitized UTF-8 string. Non-UTF-8 bytes are replaced with ``0x21`` (``!``).
  This is the legacy behavior and is appropriate for TLV values known to contain only ASCII/UTF-8 text
  (e.g., authority TLV ``0x02``).

**HEX_STRING**
  The TLV value is stored as a lowercase hex-encoded string (e.g., ``00afc7ee0ac80002``). This is safe
  for all binary payloads and preserves the original bytes without any corruption.

  Example: Google Cloud PSC sends ``pscConnectionId`` as an 8-byte big-endian uint64 in TLV type ``0xE0``.
  With ``HEX_STRING``, a ``pscConnectionId`` of ``49477946121388034`` is stored as ``"00afc7ee0ac80002"``.

.. code-block:: yaml

  listener_filters:
    - name: envoy.filters.listener.proxy_protocol
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol
        rules:
          - tlv_type: 0xE0
            on_tlv_present:
              key: "psc_conn_id"
              value_format: HEX_STRING
          - tlv_type: 0x02
            on_tlv_present:
              key: "authority"

This implementation supports both version 1 and version 2, it
automatically determines on a per-connection basis which of the two
versions is present.

.. note::
  If the filter is enabled, the Proxy Protocol must be present on the connection (either version 1 or version 2).
  The standard does not allow parsing to determine if it is present or not. However, the filter can be configured
  to allow the connection to be accepted without the Proxy Protocol header (against the standard).
  See :ref:`allow_requests_without_proxy_protocol <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.allow_requests_without_proxy_protocol>`.

If there is a protocol error or an unsupported address family
(e.g. AF_UNIX) the connection will be closed and an error thrown.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol>`

Statistics
----------

This filter emits the following general statistics, rooted at *proxy_proto.[<stat_prefix>.]*

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  not_found_disallowed, Counter, "Total number of connections that don't contain the PROXY protocol header and are rejected."
  not_found_allowed, Counter, "Total number of connections that don't contain the PROXY protocol header, but are allowed due to :ref:`allow_requests_without_proxy_protocol <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.allow_requests_without_proxy_protocol>`."

The filter also emits the statistics rooted at *proxy_proto.[<stat_prefix>.]versions.<version>*
for each matched PROXY protocol version. Proxy protocol versions include ``v1`` and ``v2``.

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  found, Counter, "Total number of connections where the PROXY protocol header was found and parsed correctly."
  disallowed, Counter, "Total number of ``found`` connections that are rejected due to :ref:`disallowed_versions <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.disallowed_versions>`."
  error, Counter, "Total number of connections where the PROXY protocol header was malformed (and the connection was rejected)."

The filter also emits the following legacy statistics, rooted at its own scope and **not** including the *stat_prefix*:

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  downstream_cx_proxy_proto_error, Counter, "Total number of connections with proxy protocol errors, i.e. ``v1.error``, ``v2.error``, and ``not_found_disallowed``."

.. attention::
  Prefer using the more-detailed non-legacy statistics above.
