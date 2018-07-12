.. _config_http_filters_grpc_json_transcoder:

gRPC-JSON transcoder
====================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v1 API reference <config_http_filters_grpc_json_transcoder_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.transcoder.v2.GrpcJsonTranscoder>`

This is a filter which allows a RESTful JSON API client to send requests to Envoy over HTTP
and get proxied to a gRPC service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

.. _config_grpc_json_generate_proto_descriptor_set:

How to generate proto descriptor set
------------------------------------

Envoy has to know the proto descriptor of your gRPC service in order to do the transcoding.

To generate a protobuf descriptor set for the gRPC service, you'll also need to clone the
googleapis repository from GitHub before running protoc, as you'll need annotations.proto
in your include path, to define the HTTP mapping.

.. code-block:: bash

  git clone https://github.com/googleapis/googleapis
  GOOGLEAPIS_DIR=<your-local-googleapis-folder>

Then run protoc to generate the descriptor set from bookstore.proto:

.. code-block:: bash

  protoc -I$(GOOGLEAPIS_DIR) -I. --include_imports --include_source_info \
    --descriptor_set_out=proto.pb test/proto/bookstore.proto

If you have more than one proto source files, you can pass all of them in one command.

Route configs for transcoded requests
-------------------------------------

The route configs to be used with the gRPC-JSON transcoder should be identical to the gRPC route.
The requests processed by the transcoder filter will have `/<package>.<service>/<method>` path and
`POST` method. The route configs for those requests should match on `/<package>.<service>/<method>`,
not the incoming request path. This allows the routes to be used for both gRPC requests and
gRPC-JSON transcoded requests.

For example, with the following proto example, the router will process `/helloworld.Greeter/SayHello`
as the path, so the route config prefix `/say` won't match requests to `SayHello`. If you want to
match the incoming request path, set `match_incoming_request_route` to true.

.. code-block:: proto

  package helloworld;

  // The greeting service definition.
  service Greeter {
    // Sends a greeting
    rpc SayHello (HelloRequest) returns (HelloReply) {
      option (google.api.http) = {
        get: "/say"
      };
    }
  }

Sending arbitrary content
-------------------------

By default, when transcoding occurs, gRPC-JSON encodes the message output of a gRPC service method into
JSON and sets the HTTP response `Content-Type` header to `application/json`. To send abritrary content,
a gRPC service method can use
`google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_
as its output message type. The implementation needs to set
`content_type <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L68>`_
(which sets the value of the HTTP response `Content-Type` header) and
`data <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L71>`_
(which sets the HTTP response body) accordingly.