HTTP/1.1 Header Casing
======================

When handling HTTP/1.1, Envoy will normalize the header keys to be all lowercase. While this is
compliant with the HTTP/1.1 spec, in practice this can result in issues when migrating
existing systems that might rely on specific header casing.

To support these use cases, Envoy allows configuring a formatting scheme for the headers, which
will have Envoy transform the header keys during serialization. To configure this formatting on
response headers, specify the format in the :ref:`http_protocol_options <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.http_protocol_options>`.
To configure this for upstream request headers, specify the formatting on the :ref:`Cluster <envoy_api_field_Cluster.http_protocol_options>`.
