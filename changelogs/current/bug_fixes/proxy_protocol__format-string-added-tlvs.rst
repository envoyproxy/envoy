Fixed the upstream ``proxy_protocol`` transport socket ignoring
:ref:`added_tlvs <envoy_v3_api_field_config.core.v3.ProxyProtocolConfig.added_tlvs>` entries that
use ``format_string`` instead of a static ``value``. These entries are now evaluated against the
upstream connection stream info and emitted in the proxy protocol v2 header. An entry that sets
both ``value`` and ``format_string``, or neither, is now rejected at configuration load. This
behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.proxy_protocol_added_tlvs_format_string`` to false.
