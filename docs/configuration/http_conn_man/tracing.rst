.. _config_http_conn_man_tracing:

Tracing
=======

.. code-block:: json
  
  {
    "tracing": {
      "operation_name": "...",
      "request_headers_for_tags": []
    }
  }
 
operation_name
  *(required, string)* Span name will be derived from operation_name. "ingress" and "egress"
  are the only supported values.

request_headers_for_tags
  *(optional, array)* An optional list of header names which is used for populating tags on the active span.
  Each tag name is the header name, and tag value is the header value from the request headers.
  The tag is only created if specified header is present in the request headers. Otherwise, it's skipped.  


