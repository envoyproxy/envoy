.. _config_http_filters_cdn_loop:

CDN-Loop header
===============

The CDN-Loop header filter participates in the cross-CDN loop detection protocol specified by `RFC
8586 <https://tools.ietf.org/html/rfc8586>`_. The CDN-Loop header filter performs two actions.
First, the filter checks to see how many times a particular CDN identifier has appeared in the
CDN-Loop header. Next, if the check passes, the filter then appends the CDN identifier to the
CDN-Loop header and passes the request to the next upstream HTTP filter. If the check fails, the filter
stops processing on the request and returns an error response.

RFC 8586 is particular in how the CDN-Loop header should be modified. As such:

* other filters in the filter chain should not modify the CDN-Loop header and
* the HTTP route configuration's :ref:`request_headers_to_add
  <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_add>` or
  :ref:`request_headers_to_remove
  <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_remove>` fields should
  not contain the CDN-Loop header.

The filter will coalesce multiple CDN-Loop headers into a single, comma-separated header.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.cdn_loop.v3.CdnLoopConfig``.

The :ref:`filter config <envoy_v3_api_msg_extensions.filters.http.cdn_loop.v3.CdnLoopConfig>` has two fields.

* The *cdn_id* field sets the identifier that the filter will look for within and append to the
  CDN-Loop header. RFC 8586 calls this field the "cdn-id"; "cdn-id" can either be a pseudonym or a
  hostname the CDN provider has control of. The *cdn_id* field must not be empty.
* The *max_allowed_occurrences* field controls how many times *cdn_id* can appear in the CDN-Loop
  header on downstream requests (before the filter appends *cdn_id* to the header). If the *cdn_id*
  appears more than *max_allowed_occurrences* times in the header, the filter will reject the
  downstream's request. Most users should configure *max_allowed_occurrences* to be 0 (the
  default).

Response Code Details
---------------------

.. list-table::
   :header-rows: 1

   * - Name
     - HTTP Status
     - Description
   * - invalid_cdn_loop_header
     - 400 (Bad Request)
     - The CDN-Loop header in the downstream is invalid or unparseable.
   * - cdn_loop_detected
     - 502 (Bad Gateway)
     - The *cdn_id* value appears more than *max_allowed_occurrences* in the CDN-Loop header,
       indicating a loop between CDNs.

