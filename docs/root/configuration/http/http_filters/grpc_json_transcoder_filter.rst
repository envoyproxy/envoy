.. _config_http_filters_grpc_json_transcoder:

gRPC-JSON transcoder
====================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder>`
* This filter should be configured with the name *envoy.filters.http.grpc_json_transcoder*.

This is a filter which allows a RESTful JSON API client to send requests to Envoy over HTTP
and get proxied to a gRPC service. The HTTP mapping for the gRPC service has to be defined by
`custom options <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_.

JSON mapping
------------

The protobuf to JSON mapping is defined `here <https://developers.google.com/protocol-buffers/docs/proto3#json>`_. For
gRPC stream request parameters, Envoy expects an array of messages, and it returns an array of messages for stream
response parameters.

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
JSON and sets the HTTP response `Content-Type` header to `application/json`. To send arbitrary content,
a gRPC service method can use
`google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_
as its output message type. The implementation needs to set
`content_type <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L68>`_
(which sets the value of the HTTP response `Content-Type` header) and
`data <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L71>`_
(which sets the HTTP response body) accordingly.
Multiple `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_
can be send by the gRPC server in the server streaming case.
In this case, HTTP response header `Content-Type` will use the `content-type` from the first
`google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_.

Headers
--------

gRPC-JSON forwards the following headers to the gRPC server:

* `x-envoy-original-path`, containing the value of the original path of HTTP request
* `x-envoy-original-method`, containing the value of the original method of HTTP request


Sample Envoy configuration
--------------------------

Here's a sample Envoy configuration that proxies to a gRPC server running on localhost:50051. Port 51051 proxies
gRPC requests and uses the gRPC-JSON transcoder filter to provide the RESTful JSON mapping. I.e., you can make either
gRPC or RESTful JSON requests to localhost:51051.

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 0.0.0.0, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener1
      address:
        socket_address: { address: 0.0.0.0, port_value: 51051 }
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: grpc_json
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                # NOTE: by default, matching happens based on the gRPC route, and not on the incoming request path.
                # Reference: https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/grpc_json_transcoder_filter#route-configs-for-transcoded-requests
                - match: { prefix: "/helloworld.Greeter" }
                  route: { cluster: grpc, timeout: { seconds: 60 } }
            http_filters:
            - name: envoy.filters.http.grpc_json_transcoder
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
                proto_descriptor: "/tmp/envoy/proto.pb"
                services: ["helloworld.Greeter"]
                print_options:
                  add_whitespace: true
                  always_print_primitive_fields: true
                  always_print_enums_as_ints: false
                  preserve_proto_field_names: false
            - name: envoy.filters.http.router

    clusters:
    - name: grpc
      connect_timeout: 1.25s
      type: logical_dns
      lb_policy: round_robin
      dns_lookup_family: V4_ONLY
      http2_protocol_options: {}
      load_assignment:
        cluster_name: grpc
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  # WARNING: "docker.for.mac.localhost" has been deprecated from Docker v18.03.0.
                  # If you're running an older version of Docker, please use "docker.for.mac.localhost" instead.
                  # Reference: https://docs.docker.com/docker-for-mac/release-notes/#docker-community-edition-18030-ce-mac59-2018-03-26
                  address: host.docker.internal
                  port_value: 50051

