.. _config_http_conn_man_header_casing:

HTTP/1.1 Header Casing
======================

When handling HTTP/1.1, Envoy will normalize the header keys to be all lowercase. While this is
compliant with the HTTP/1.1 spec, in practice this can result in issues when migrating
existing systems that might rely on specific header casing.

To support these use cases, Envoy allows :ref:`configuring a formatting scheme for the headers
<envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.header_key_format>`, which will have Envoy
transform the header keys during serialization.

To configure this formatting on response headers, specify the format in the
:ref:`http_protocol_options
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_protocol_options>`.
To configure this for upstream request headers, specify the formatting in
:ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` in
the cluster's
:ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`.

Currently Envoy supports two mutually exclusive types of header key formatters:

Stateless formatters
--------------------

Stateless formatters are run on encoding and do not depend on any previous knowledge of the headers.
An example of this type of formatter is the :ref:`proper case words
<envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.HeaderKeyFormat.proper_case_words>`
formatter. These formatters are useful when converting from non-HTTP/1 to HTTP/1 (within a single
proxy or across multiple hops) or when stateful formatting is not desired due to increased memory
requirements.

Stateful formatters
-------------------

Stateful formatters are instantiated on decoding, called for every decoded header, attached to the
header map, and are then available during encoding to format the headers prior to writing. Thus, they
traverse the entire proxy stack. An example of this type of formatter is the :ref:`preserve case
formatter
<envoy_v3_api_msg_extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig>`
configured via the :ref:`stateful_formatter
<envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.HeaderKeyFormat.stateful_formatter>` field.
The following is an example configuration which will preserve HTTP/1 header case across the proxy.

.. note::

   When Stateful formatters are used, headers added internally by Envoy or by filters (e.g. the Lua filter) will still be lowercased.

.. literalinclude:: _include/preserve-case.yaml
    :language: yaml
