.. _config_http_filters_rate_limit_v1:

Rate limit
==========

Rate limit :ref:`configuration overview <config_http_filters_rate_limit>`.

.. code-block:: json

  {
    "name": "rate_limit",
    "config": {
      "domain": "...",
      "stage": "...",
      "request_type": "...",
      "timeout_ms": "..."
    }
  }

domain
  *(required, string)* The rate limit domain to use when calling the rate limit service.

stage
  *(optional, integer)* Specifies the rate limit configurations to be applied with the same stage
  number. If not set, the default stage number is 0.

  **NOTE:** The filter supports a range of 0 - 10 inclusively for stage numbers.

request_type
  *(optional, string)* The type of requests the filter should apply to. The supported
  types are *internal*, *external* or *both*. A request is considered internal if
  :ref:`x-envoy-internal<config_http_conn_man_headers_x-envoy-internal>` is set to true. If
  :ref:`x-envoy-internal<config_http_conn_man_headers_x-envoy-internal>` is not set or false, a
  request is considered external. The filter defaults to *both*, and it will apply to all request
  types.

timeout_ms
  *(optional, integer)* The timeout in milliseconds for the rate limit service RPC. If not set,
  this defaults to 20ms.
