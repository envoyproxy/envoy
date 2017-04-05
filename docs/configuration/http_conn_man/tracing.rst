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
  *(optional, array)* A list of header names used to create tags for the active span.
   The header name is used to populate the tag name, and the header value is used to populate the tag value.
   The tag is created if the specified header name is present in the request's headers. 

