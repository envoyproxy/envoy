.. _config_http_filters_grpc_json_reverse_transcoder:

gRPC-JSON reverse transcoder
============================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.grpc_json_reverse_transcoder.v3.GrpcJsonReverseTranscoder``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_json_reverse_transcoder.v3.GrpcJsonReverseTranscoder>`

This is a filter which allows a gRPC client to send requests to Envoy and get proxied
to a RESTful JSON API service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

Unsupported options from ``google.api.http``
--------------------------------------------
* **additional_binding**: The gRPC-JSON reverse transcoder ignores this options, if set, as it makes binding a
  gRPC method to a single HTTP endpoint difficult.
* **response_body**: The gRPC-JSON reverse transcoder uses the whole response from the upstream service
  as a response to the gRPC client and ignores this options, if set.

JSON mapping
------------

The protobuf to JSON mapping is defined `here <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.

* **NOTE:** The gRPC-JSON reverse transcoder ignores the ``json_name`` option and uses the proto field names
  as the JSON names by default.

.. _config_grpc_json_reverse_transcoder_generate_proto_descriptor_set:

How to generate proto descriptor set
------------------------------------

Envoy has to know the proto descriptor of your gRPC service in order to do the transcoding.

To generate a protobuf descriptor set for the gRPC service, you'll also need to clone the
googleapis repository from GitHub before running ``protoc``, as you'll need annotations.proto
in your include path, to define the HTTP mapping.

.. code-block:: console

  $ git clone https://github.com/googleapis/googleapis
  $ GOOGLEAPIS_DIR=<your-local-googleapis-folder>

Then run ``protoc`` to generate the descriptor set. For example using the test
:repo:`bookstore.proto <test/proto/bookstore.proto>` provided in the Envoy repository:

.. code-block:: console

  $ protoc -I${GOOGLEAPIS_DIR} -I. --include_imports --include_source_info \
      --descriptor_set_out=proto.pb test/proto/bookstore.proto

If you have more than one proto source files, you can pass all of them in one command.

Sample Envoy configuration
--------------------------

Here's a sample Envoy configuration that proxies to a RESTful JSON server running on localhost:50051. Port 51051 proxies
HTTP requests and uses the gRPC-JSON reverse transcoder filter to provide the gRPC mapping. I.e., you can make either
gRPC or RESTful JSON requests to localhost:51051.

.. literalinclude:: _include/grpc-json-reverse-transcoder-filter.yaml
    :language: yaml
    :linenos:
    :lines: 9-35
    :emphasize-lines: 11-14
    :caption: :download:`grpc-json-reverse-transcoder-filter.yaml <_include/grpc-json-reverse-transcoder-filter.yaml>`
