.. _config_http_conn_man_tracing:

Tracing
=======

.. code-block:: json
  
  {
    "tracing": {
      "operation_name": "..."
    }
  }
 
operation_name
  *(required, string)* Span name will be derived from operation_name. "ingress" and "egress"
  are the only supported values.


