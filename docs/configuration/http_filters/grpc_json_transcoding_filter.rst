.. _config_http_filters_grpc_json_transcoding:

gRPC-JSON transcoding filter
============================

gRPC :ref:`architecture overview <arch_overview_grpc>`.

This is a filter which enables the bridging of a JSON-REST client to a gRPC server.

.. code-block:: json

  {
    "type": "both",
    "name": "grpc_json_transcoding",
    "config": {
      "proto_descriptors": "proto.pb",
      "services": ["grpc.service.Service"]
    }
  }
