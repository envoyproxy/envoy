Added the ``UNSIGNED_INTEGER_BIG_ENDIAN`` :ref:`value_string_encoding
<envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.KeyValuePair.value_string_encoding>`
to the PROXY protocol listener filter, which stores a 1 to 8 byte TLV value as a decimal string, and
the :ref:`reject_if_absent
<envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.Rule.reject_if_absent>`
rule option, which closes connections that do not carry the TLV. The new ``tlv_invalid`` and
``tlv_missing`` statistics count values that cannot be encoded and rejected connections.
