Added the ``envoy.ai.llm_protocol.request`` and ``envoy.ai.upstream_target`` filter state keys to
the :ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager_protocol_filter_state>`
(alpha). The first names the client's wire API ahead of the route's declaration; the second is the
complete description of the upstream a request is sent to, carrying the API that upstream speaks,
which AI filters see and token usage is extracted in. Only trusted, configuration-driven writers
may set the second, and never from request content.
