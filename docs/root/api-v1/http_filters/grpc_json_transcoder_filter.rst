.. _config_http_filters_grpc_json_transcoder_v1:

gRPC-JSON transcoder filter
===========================

gRPC-JSON transcoder :ref:`configuration overview <config_http_filters_grpc_json_transcoder>`.

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
  *(required, string)* Supplies the filename of
  :ref:`the proto descriptor set <config_grpc_json_generate_proto_descriptor_set>` for the gRPC
  services.

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
    output easy to read. Defaults to false.

  always_print_primitive_fields
    *(optional, boolean)* Whether to always print primitive fields. By default primitive
    fields with default values will be omitted in JSON output. For
    example, an int32 field set to 0 will be omitted. Setting this flag to
    true will override the default behavior and print primitive fields
    regardless of their values. Defaults to false.

  always_print_enums_as_ints
    *(optional, boolean)* Whether to always print enums as ints. By default they are rendered
    as strings. Defaults to false.

  preserve_proto_field_names
    *(optional, boolean)* Whether to preserve proto field names. By default protobuf will
    generate JSON field names using the ``json_name`` option, or lower camel case,
    in that order. Setting this flag will preserve the original field names. Defaults to false.
