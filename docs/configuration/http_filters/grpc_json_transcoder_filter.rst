.. _config_http_filters_grpc_json_transcoder:

gRPC-JSON transcoder filter
===========================

gRPC :ref:`architecture overview <arch_overview_grpc>`.

This is a filter which allows a RESTful JSON API client to send requests to Envoy over HTTP
and get proxied to a gRPC service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

Configure gRPC-JSON transcoder
------------------------------

The filter config for the filter requires the descriptor file as well as a list of the gRPC
services to be transcoded.

.. code-block:: json

  {
    "name": "grpc_json_transcoder",
    "config": {
      "proto_descriptor": "proto.pb",
      "services": ["grpc.service.Service"],
      "print_options": {
        "add_whitespace": false,
        "always_print_primitive_fields": false,
        "always_print_enums_as_ints": false,
        "preserve_proto_field_names": false
      }
    }
  }

proto_descriptor
  *(required, string)* Supplies the binary protobuf descriptor set for the gRPC services.
  The descriptor set has to include all of the types that are used in the services. Make sure
  to use the ``--include_import`` option for ``protoc``.

  To generate a protobuf descriptor set for the gRPC service, you'll also need to clone the
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
  transcoder will translate. If the service name doesn't exist in ``proto_descriptor``, Envoy
  will fail at startup. The ``proto_descriptor`` may contain more services than the service names
  specified here, but they won't be translated.

print_options
  *(optional, object)* Control options for response json. These options are passed directly to
  `JsonPrintOptions <https://developers.google.com/protocol-buffers/docs/reference/cpp/
  google.protobuf.util.json_util#JsonPrintOptions>`_. Valid options are:

  add_whitespace
    *(optional, boolean)* Whether to add spaces, line breaks and indentation to make the JSON
    output easy to read. Default to false.

  always_print_primitive_fields
    *(optional, boolean)* Whether to always print primitive fields. By default primitive fields
    with default values will be omitted in JSON output. For example, an int32 field set to 0
    will be omitted. Set this flag to true will override the default behavior and print primitive
    fields regardless of their values. Default to false.

  always_print_enums_as_ints
    *(optional, boolean)* Whether to always print enums as ints. By default they are rendered as
    strings. Default to false.

  preserve_proto_field_names
    *(optional, boolean)* Whether to preserve proto field names. By default protobuf will generate
    JSON field names use ``json_name`` option, or lower camel case, in that order. Set this flag
    will preserve original field names. Default to false.
