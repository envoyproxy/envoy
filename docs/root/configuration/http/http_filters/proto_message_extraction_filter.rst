.. _config_http_filters_proto_message_extraction:

Proto Message Extraction
========================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.proto_message_extraction.v3.ProtoMessageExtractionConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.proto_message_extraction.v3.ProtoMessageExtractionConfig>`.

ProtoMessageExtraction filter supports extracting gRPC
requests/responses (proto messages) into google.protobuf.Struct and storing
results in the dynamic metadata `envoy.filters.http.proto_message_extraction`
for later access.

Use Case
--------

The ProtoMessageExtraction filter is particularly useful in scenarios where
sensitive or detailed logging of gRPC requests and responses is required.
In Client-Side Streaming or Server-Side Streaming, the filter can store the
first and last messages, which can later be used for logging or for obtaining
a comprehensive view of the data flow.

Assumptions
-----------

This filter assumes it is only applicable for gRPC with Protobuf as payload.

Process Flow
------------

On the request and response path, it will check

1. if the incoming gRPC request/response is configured, the filter tries to:

  a. buffer the incoming data to complete protobuf messages
  b. extract individual protobuf messages according to directives
  c. write the result into the dynamic metadata
  d. pass through the request/response data

2. otherwise, pass through the request.

The extraction process in this filter is not on the critical path, as it does not
modify the request or response. The filter extracts the specified fields,
writes them to dynamic metadata, and then passes the request/response
through without modification.

Config Requirements
-------------------

Here are config requirements

1. the extract target field should be among the following primitive types:
`string`, `uint32`, `uint64`, `int32`, `int64`, `sint32`, `sint64`,
`fixed32`, `fixed64`, `sfixed32`, `sfixed64`, `float`, `double`.

2. the target field could be repeated.

3. the intermediate type could also be repeated.

Output Format
-------------

The extracted requests and responses will be  will be added in the dynamic
``metadata<google.protobuf.Struct>`` with the same layout of the message.

For the default `FIRST_AND_LAST` mode, the output will be like:

Case: Non-Streaming requests/response

.. code-block:: json

  {
    "requests":{
       "first":{
          "foo": "val_foo1",
       }
    },
    "responses":{
       "first":{
          "baz": "val_baz1",
       }
    }
  }


Case: Streaming requests/response

.. code-block:: json

  {
    "requests":{
       "first":{
          "foo": "val_foo1",
       }
       "last":{
          "foo": "val_foo3",
       }
    },
    "responses":{
       "first":{
          "baz": "val_baz1",
       }
       "last":{
          "baz": "val_foo3",
       }
    }
  }

For more details, please refer to the
:ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.proto_message_extraction.v3.ProtoMessageExtractionConfig>`.
