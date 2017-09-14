.. _config_http_filters_grpc_web:

gRPC-Web filter
====================

gRPC :ref:`architecture overview <arch_overview_grpc>`.

This is a filter which enables the bridging of a gRPC-Web client to a compliant gRPC server by
following https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md.

.. code-block:: json

  {
    "name": "grpc_web",
    "config": {}
  }
