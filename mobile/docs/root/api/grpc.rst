.. _api_grpc:

gRPC streams
============

Envoy Mobile provides support for gRPC as a thin interface built on top of its :ref:`HTTP APIs <api_http>`.

gRPC APIs are designed to be used in conjunction with protobuf libraries such as
`SwiftProtobuf <https://github.com/apple/swift-protobuf>`_ and
`Java Protobuf <https://github.com/protocolbuffers/protobuf/tree/master/java>`_.

Envoy Mobile implements the gRPC protocol, accepting and returning serialized protobuf models.

.. note::

  In the future, Envoy Mobile will provide much more comprehensive integration with gRPC and protobuf,
  utilizing annotations for enhanced functionality.

--------------
``GRPCClient``
--------------

The ``GRPCClient`` type provides the ability to start gRPC streams, and is backed by Envoy Mobile's
``HTTPClient`` type that is instantiated using the ``EnvoyClientBuilder``.

To create a ``GRPCClient``, simply :ref:`create an HTTP client <api_starting_envoy>` and pass it to the initializer:

``grpcClient = GRPCClient(httpClient)``

This client can then be used with the types outlined below for starting gRPC streams.

----------------------
``GRPCRequestBuilder``
----------------------

Envoy Mobile provides a ``GRPCRequestBuilder`` which acts very similarly to the ``RequestBuilder``
type. Upon calling ``build()``, it returns a ``Request`` (the same type used for standard HTTP
requests/streams) which is preconfigured for gRPC.

To start a gRPC stream, create a ``Request`` using the ``GRPCRequestBuilder``.

**Kotlin**::

  val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "api.envoyproxy.io", true)
    .addHeader("x-custom-header", "foobar")
    ...
    .build()

**Swift**::

  let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar", authority: "api.envoyproxy.io", useHTTPS: true)
    .addHeader(name: "x-custom-header", value: "foobar")
    ...
    .build()

-----------------------
``GRPCResponseHandler``
-----------------------

Very similarly to the HTTP ``ResponseHandler``, the ``GRPCResponseHandler`` allows for receiving
updates to the gRPC stream and contains a set of callbacks that are called whenever an update
occurs on the stream.

This handler processes inbound gRPC responses, buffers data as necessary while chunks of
protobuf messages are received, then finally passes fully formed protobuf data to the callbacks
provided.

Typically, consumers should listen to ``onMessage`` and use a protobuf library to deserialize
the complete protobuf message data.

**Kotlin**::

  val handler = GRPCResponseHandler(Executor { })
    .onHeaders { headers, grpcStatus, _ ->
      Log.d("MainActivity", "Received gRPC status: " + statusCode + " and headers: " + headers)
      Unit
    }
    .onMessage { messageData ->
      // Deserialize message data here
    }
    .onTrailers { trailers ->
      Log.d("MainActivity", "Trailers received: " + trailers)
      Unit
    }
    .onError { error ->
      Log.d("MainActivity", "Error received: " + error.message)
      Unit
    }
    ...

**Swift**::

  let handler = GRPCResponseHandler()
    .onHeaders { headers, grpcStatus, _ in
      print("Received gRPC status: \(grpcStatus) and headers: \(headers)")
    }
    .onMessage { messageData in
      // Deserialize message data here
    }
    .onTrailers { trailers in
      print("Trailers received: \(trailers)")
    }
    .onError { error in
      print("Error received: \(error.message)")
    }
    ...


---------------------
``GRPCStreamEmitter``
---------------------

Finally, a gRPC stream may be opened using a ``GRPCClient`` instance.

Doing so returns a ``GRPCStreamEmitter`` which allows the sender to interact with the stream.

The ``sendMessage`` function should be invoked with the serialized data from a protobuf message.
The emitter will then transform the provided data into the gRPC wire format and send it over the
stream.

**Kotlin**::

  val envoy = AndroidEnvoyClientBuilder(...).build()
  val grpcClient = GRPCClient(envoy)

  val request = GRPCRequestBuilder(...).build()
  val responseHandler = GRPCResponseHandler(...)
  val grpcEmitter = grpcClient.start(request, responseHandler)
    .sendMessage(...)
    .sendMessage(...)

  ...
  grpcEmitter.close(...)

**Swift**::

  let envoy = try EnvoyClientBuilder(...).build()
  let grpcClient = GRPCClient(httpClient: envoy)

  let request = GRPCRequestBuilder(...).build()
  let responseHandler = GRPCResponseHandler(...)
  let grpcEmitter = grpcClient.start(request, handler: responseHandler)
    .sendMessage(...)
    .sendMessage(...)

  ...
  grpcEmitter.close(...)
