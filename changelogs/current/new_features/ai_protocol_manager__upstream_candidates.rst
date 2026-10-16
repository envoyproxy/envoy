Added :ref:`upstream candidates <config_http_filters_ai_protocol_manager_upstream_candidates>` to
the :ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>` (alpha). An AI
filter whose extension allows it may seed, through its ``AiFilterCallbacks``, an ordered list of
clusters the request may go to, each with an optional model and path rewrite. Once the AI filters
are done, the filter commits the first eligible candidate through a ``priority_group`` cluster
specifier configured with ``override_metadata_namespace: envoy.ai.upstream.candidates``, rewrites
the request only if the route followed, and stores the result as the
``envoy.ai.upstream.candidates`` filter state object. Commits are counted by the new
``upstream_candidates_*`` statistics.
