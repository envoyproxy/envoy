The :ref:`custom response filter <config_http_filters_custom_response>` can now
match on request properties such as request header values (for example the ``Accept`` header) in its
:ref:`matcher <envoy_v3_api_field_extensions.filters.http.custom_response.v3.CustomResponse.custom_response_matcher>`,
in addition to the response status code and response headers.
