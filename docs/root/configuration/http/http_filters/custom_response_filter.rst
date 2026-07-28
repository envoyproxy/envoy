.. _config_http_filters_custom_response:

Custom Response Filter
======================
This filter is used to override responses from upstream (primarily error responses) with locally defined responses, or via redirection to another upstream.

Configuration
-------------
The filter configuration consists of a matcher that matches the original response to specific custom response policies to be used to override the response.

The matcher can match on the response status code and response headers, as well as on request header values. Matching against request inputs (for example ``HttpRequestHeaderMatchInput``) evaluates the original downstream request, which makes it possible to select a custom response based on the request, e.g. returning a JSON or HTML error body depending on the ``Accept`` request header.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.custom_response.v3.CustomResponse>`

------------------------

Custom response policies define where from and how to retrieve custom responses once a response is matched to a particular policy by the matcher.

Redirect policy
###############

The redirect policy can be used to override the original response by internally redirecting it to a different route by modifying the host and path of the original request. The policy config can be used to modify both the request and response headers and the response status code.

* This extension should be configued with the type URL ``type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.custom_response.redirect_policy.v3.RedirectPolicy>`

Local Response Policy
#####################

The local response policy can be used to override the original response with a locally stored response body. The policy config can be used to modify the response headers and the response status code.

* This extension should be configued with the type URL ``type.googleapis.com/envoy.extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy>`
