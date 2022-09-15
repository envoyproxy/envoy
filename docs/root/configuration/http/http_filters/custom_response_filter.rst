.. _config_http_filters_custom_response:

Custom Response Filter
======================
This filter is used to override responses from upstream (primarily error responses) with locally defined responses, or via redirection to another upstream.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.custom_response.v3.CustomResponse>`
