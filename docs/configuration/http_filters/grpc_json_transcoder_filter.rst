.. _config_http_filters_grpc_json_transcoding:

gRPC-JSON transcoder filter
===========================

gRPC :ref:`architecture overview <arch_overview_grpc>`.

This is a filter which allows a RESTful JSON API client to send requests to Envoy over HTTP
and get proxied to a gRPC service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

Configure gRPC-JSON transcoder
------------------------------

To configure gRPC-JSON transcoder, you'll need to generate protobuf descriptors from your gRPC
service. You'll also need to clone the googleapis repository from Github before running protoc,
as you'll need annotations.proto in your include path.

.. code-block:: bash

  git clone https://github.com/googleapis/googleapis
  GOOGLEAPIS_DIR=<your-local-googleapis-folder>

Then run protoc to generate .pb descriptor from bookstore.proto:

.. code-block:: bash

  protoc -I$(GOOGLEAPIS_DIR) -I. --include_imports --include_source_info \
    test/proto/bookstore.proto --descriptor_set_out=proto.pb

The filter config for the filter need the descriptor file, and the services you want to enable.

.. code-block:: json

  {
    "type": "both",
    "name": "grpc_json_transcoder",
    "config": {
      "proto_descriptors": "proto.pb",
      "services": ["grpc.service.Service"]
    }
  }
