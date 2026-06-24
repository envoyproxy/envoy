Added :ref:`encoding
<envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.KeyValuePair.value_string_encoding>` option to
control how a TLV value is encoded before it is stored in dynamic metadata or filter state. By default the TLV
value is sanitized to a valid UTF-8 string (previous behavior). Setting it to ``BASE64`` stores the raw TLV value
as a base64-encoded string instead.
