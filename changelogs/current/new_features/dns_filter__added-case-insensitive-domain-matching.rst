Added :ref:`case_insensitive
<envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.case_insensitive>` to the
DNS filter. When set, virtual domain names are matched case-insensitively while the response still
echoes the client's original query-name case. Defaults to ``false``.
