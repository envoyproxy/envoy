.. _config_http_conn_man_tracing:

Tracing
=======

.. code-block:: json
  
  {
    "tracing": {
      "operation_name": "...",
      "type": "..."
    }
  }
 
operation_name
  *(required, string)* Span name that will be emitted on completed request.
  
type
  *(optional, string)* Allows filtering of requests so that only some of them are traced. Default 
  value is *all*. Possible values are:
    
  all
    Trace all requests.

  upstream_failure
    Trace only requests for which an upstream failure occurred.

