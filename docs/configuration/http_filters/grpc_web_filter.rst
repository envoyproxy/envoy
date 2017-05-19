.. _config_http_filters_grpc_web:

gRPC-Web filter
====================

This is a filter which enables the bridging of a gRPC-Web client to a compliant gRPC server by
following https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md.

.. code-block:: json

  {
    "type": "both",
    "name": "grpc_web",
    "config": {}
  }
