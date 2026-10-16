With :ref:`allow_dynamic_host_from_filter_state
<envoy_v3_api_field_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig.allow_dynamic_host_from_filter_state>`,
the :ref:`dynamic forward proxy HTTP filter <config_http_filters_dynamic_forward_proxy>` now resolves every host of the
``envoy.upstream.dynamic_host_candidates`` filter state before the request continues, and fails the request only when
none of the hosts resolves.
