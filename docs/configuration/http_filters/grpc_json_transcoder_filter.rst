.. _config_http_filters_grpc_json_transcoder:

gRPC-JSON transcoder filter
===========================

gRPC :ref:`architecture overview <arch_overview_grpc>`.

This is a filter which allows a RESTful JSON API client to send requests to Envoy over HTTP
and get proxied to a gRPC service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

Configure gRPC-JSON transcoder
------------------------------

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

proto_descriptors
  *(required, string)* Supplies binary protobuf descriptor set for the gRPC services.
  The descriptor set have to include all types that are used in the services. Make sure to use
  ``--include_import`` option for ``protoc``.

  To generate protobuf descriptor set for the gRPC service, you'll also need to clone the
  googleapis repository from Github before running protoc, as you'll need annotations.proto
  in your include path.

  .. code-block:: bash

    git clone https://github.com/googleapis/googleapis
    GOOGLEAPIS_DIR=<your-local-googleapis-folder>

  Then run protoc to generate the descriptor set from bookstore.proto:

  .. code-block:: bash

    protoc -I$(GOOGLEAPIS_DIR) -I. --include_imports --include_source_info \
      --descriptor_set_out=proto.pb test/proto/bookstore.proto

  If you have more than one proto source files, you can pass all of them in one command.

services
  *(required, array)* A list of strings that supplies the service names that the
  transcoder will translate. If the service name doesn't exist in ``proto_descriptors``, Envoy
  will fail at startup. The ``proto_descriptors`` may contain more services than the service names
  specified here, but they won't be translated.
