HTTP/1.1 Header Casing
======================

When handling HTTP/1.1, Envoy will normalize the header keys to be all lowercase. While this is
compliant with the HTTP/1.1 spec, in practice this can result in issues when migrating
existing systems that might rely on specific header casing.

To support these use cases, Envoy allows configuring a formatting scheme for the headers, which
will have Envoy transform the header keys during serialization. To configure this formatting on
response headers, specify the format in the :ref:`http_protocol_options <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_protocol_options>`.
To configure this for upstream request headers, specify the formatting in :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` in the Cluster's :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`.

See :ref:`below <faq_configuration_timeouts_transport_socket>` for other connection timeouts.
on the :ref:`Cluster <envoy_v3_api_field_config.cluster.v3.Cluster.http_protocol_options>`. FIXME
