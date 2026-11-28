.. _config_http_ai_filters:

AI filters
==========

AI filters (category ``envoy.http.ai_filters``) run inside the :ref:`AI Protocol
Manager <config_http_filters_ai_protocol_manager>` over a request's parsed
payload and, for some, its response. They are configured, in order, in its
:ref:`filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.filters>`
list; see :ref:`AI filters <config_http_filters_ai_protocol_manager_ai_filters>`
for when they run.

.. toctree::
  :maxdepth: 2

  mcp_filter
  model_router_filter
  request_info_filter
  route_selector_filter
  schema_validation_filter
  transcoder_filter
