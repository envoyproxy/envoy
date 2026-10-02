The ``envoy.http.ai_filters.request_info`` AI filter (alpha) now stores the requested model as the
``envoy.ai.model.request`` :ref:`filter state object <well_known_filter_state>`, so filters, the
router and access logs can read it as a string.
