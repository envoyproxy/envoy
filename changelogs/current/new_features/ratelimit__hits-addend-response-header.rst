Extended the global rate limit filter so a :ref:`hits_addend
<envoy_v3_api_field_config.route.v3.RateLimit.hits_addend>` :ref:`format
<envoy_v3_api_field_config.route.v3.RateLimit.HitsAddend.format>` string may reference
response headers via ``%RESP()%`` on the :ref:`apply_on_stream_done
<envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>` path, resolving the
value from the upstream response without a dynamic-metadata hop. On the request path the
response is not yet available, so ``%RESP()%`` resolves empty and the descriptor is
dropped, as before.
