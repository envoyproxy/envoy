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

-----------
Quick start
-----------

Below are some quick examples for getting started with gRPC streams. See the individual class references
in the later sections of this page for in-depth information on how each type is used.

Start and interact with a gRPC stream in **Kotlin**::

  val headers = GRPCRequestHeadersBuilder(scheme = "https", authority = "envoyproxy.io", path = "/pb.api.v1.Foo/GetBar")
    .build()

  val streamClient = AndroidStreamClientBuilder(application).build()
    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseHeaders { headers, endStream ->
        Log.d("MainActivity", "Headers received: $headers, end stream: $endStream")
      }
      .setOnResponseMessage { messageData in
        Log.d("MainActivity", "Received gRPC message")
      }
      .setOnResponseTrailers { trailers in
        Log.d("MainActivity", "Trailers received: $trailers")
      }
      .setOnError { ... }
      .setOnCancel { ... }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(headers, false)
      .sendMessage(...)
      ...
      .close()

Start and interact with a gRPC stream in **Swift**::

  let headers = GRPCRequestHeadersBuilder(scheme: "https", authority: "envoyproxy.io", path: "/pb.api.v1.Foo/GetBar")
    .build()

  let streamClient = try StreamClientBuilder().build()
  GRPCClient(streamClient: streamClient)
    .newGRPCStreamPrototype()
    .setOnResponseHeaders { headers, endStream in
      print("Headers received: \(headers), end stream: \(endStream)")
    }
    .setOnResponseMessage { messageData in
      print("Received gRPC message")
    }
    .setOnResponseTrailers { trailers in
      print("Trailers received: \(trailers)")
    }
    .setOnError { ... }
    .setOnCancel { ... }
    .start(queue: .main)
    .sendHeaders(headers, endStream: false)
    .sendMessage(...)
    ...
    .close()

--------------
``GRPCClient``
--------------

The ``GRPCClient`` type provides the ability to start gRPC streams, and is backed by Envoy Mobile's
``StreamClient`` interface (typically instantiated using a ``StreamClientBuilder``).

To create a ``GRPCClient``, simply :ref:`create a stream client <api_starting_envoy>` and pass it to the initializer:

``grpcClient = GRPCClient(streamClient)``

This client can then be used with the types outlined below for starting gRPC streams.

----------------------
``GRPCRequestHeaders``
----------------------

Envoy Mobile provides a ``GRPCRequestHeadersBuilder`` which acts very similarly to the ``RequestHeadersBuilder``
type. Upon calling ``build()``, it returns a ``GRPCRequestHeaders`` instance - a subclass of ``RequestHeaders``
configured specifically for gRPC streams.

To start a gRPC stream, first create an instance of ``GRPCRequestHeaders`` using the ``GRPCRequestHeadersBuilder``.

**Kotlin**::

  val headers = GRPCRequestHeadersBuilder("https", "envoyproxy.io", "/pb.api.v1.Foo/GetBar")
    .add("x-foo", "123")
    ...
    .build()

**Swift**::

  let headers = GRPCRequestHeadersBuilder(scheme: "https", authority: "envoyproxy.io", path: "/pb.api.v1.Foo/GetBar")
    .add(name: "x-foo", value: "123")
    ...
    .build()

-----------------------
``GRPCStreamPrototype``
-----------------------

A ``GRPCStreamPrototype`` is used to configure gRPC streams prior to starting them by assigning callbacks
to be invoked when response data is received on the stream.

Typically, consumers should listen to ``onMessage`` and use a protobuf library to deserialize
the complete protobuf message data.

To create a ``GRPCStreamPrototype``, use an instance of ``GRPCClient``.

**Kotlin**::

  val prototype = grpcClient
    .newGRPCStreamPrototype()
    .setOnResponseHeaders { headers, endStream ->
      Log.d("MainActivity", "Headers received: $headers, end stream: $endStream")
    }
    .setOnResponseMessage { messageData ->
      Log.d("MainActivity", "Received gRPC message")
    }
    .setOnResponseTrailers { trailers ->
      Log.d("MainActivity", "Trailers received: $trailers")
    }
    .setOnError { ... }
    .setOnCancel { ... }

**Swift**::

  let prototype = grpcClient
    .newGRPCStreamPrototype()
    .setOnResponseHeaders { headers, endStream in
      print("Headers received: \(headers), end stream: \(endStream)")
    }
    .setOnResponseMessage { messageData in
      print("Received gRPC message")
    }
    .setOnResponseTrailers { trailers in
      print("Trailers received: \(trailers)")
    }
    .setOnError { ... }
    .setOnCancel { ... }

--------------
``GRPCStream``
--------------

gRPC streams are started by calling ``start()`` on a ``GRPCStreamPrototype``.

Doing so returns a ``GRPCStream`` which allows the sender to interact with the stream.

The ``sendMessage`` function should be invoked with the serialized data from a protobuf message.
The emitter will then transform the provided data into the gRPC wire format and send it over the
stream.

**Kotlin**::

  val streamClient = AndroidStreamClientBuilder()
    ...
    .build()
  val grpcClient = GRPCClient(streamClient)

  val requestHeaders = GRPCRequestHeadersBuilder()
    ...
    .build()
  val prototype = grpcClient
    .newGRPCStreamPrototype()
    ...
  val stream = prototype
    .start(Executors.newSingleThreadExecutor())
    .sendHeaders(...)
    .sendMessage(...)

  ...
  stream.close(...)

**Swift**::

  let streamClient = StreamClientBuilder()
    ...
    .build()
  let grpcClient = GRPCClient(streamClient: streamClient)

  let requestHeaders = GRPCRequestHeadersBuilder()
    ...
    .build()
  let prototype = grpcClient
    .newGRPCStreamPrototype()
    ...
  let stream = prototype
    .start(queue: .main)
    .sendHeaders(...)
    .sendMessage(...)

  ...
  stream.close(...)
