.. _api_http:

HTTP requests and streams
=========================

Streams are first-class citizens in Envoy Mobile, and are supported out-of-the-box.

Unary requests (single request / single response) are also supported using the same interfaces.

-----------
Quick start
-----------

Below are some quick examples for getting started with HTTP streams. See the individual class references
in the later sections of this page for in-depth information on how each type is used.

Start and interact with an HTTP stream in **Kotlin**::

  val streamClient = AndroidStreamClientBuilder(application).build()

  val headers = RequestHeadersBuilder(method = RequestMethod.POST, scheme = "https",  authority = "api.envoyproxy.io", path = "/foo")
    .build()
  val stream = streamClient
    .newStreamPrototype()
    .setOnResponseHeaders { headers, endStream ->
      Log.d("MainActivity", "[${headers.httpStatus}] Headers received: $headers, end stream: $endStream")
    }
    .setOnResponseData { data, endStream ->
      Log.d("MainActivity", "Received data, end stream: $endStream")
    }
    .setOnResponseTrailers { trailers ->
      Log.d("MainActivity", "Trailers received: $trailers")
    }
    .setOnError { ... }
    .setOnCancel { ... }
    .start(Executors.newSingleThreadExecutor())
    .sendHeaders(...)
    .sendData(...)

  ...
  stream.close(...)

Start and interact with an HTTP stream in **Swift**::

  let headers = RequestHeadersBuilder(method: .post, scheme: "https", authority: "api.envoyproxy.io", path: "/foo")
    .build()

  let streamClient = try StreamClientBuilder().build()
  let stream = streamClient
    .newStreamPrototype()
    .setOnResponseHeaders { headers, endStream in
      print("[\(headers.httpStatus)] Headers received: \(headers), end stream: \(endStream)")
    }
    .setOnResponseData { data, endStream in
      print("Received data, end stream: \(endStream)")
    }
    .setOnResponseTrailers { trailers in
      print("Trailers received: \(trailers)")
    }
    .setOnError { ... }
    .setOnCancel { ... }
    .start(queue: .main)
    .sendHeaders()
    .sendData(...)

  ...
  stream.close(...)

------------------
``RequestHeaders``
------------------

Creating a stream is done by initializing a ``RequestHeaders`` instance via a ``RequestHeadersBuilder``,
then passing it to a previously created :ref:`StreamClient instance <api_starting_envoy>`.

**Kotlin**::

  val headers = RequestHeadersBuilder(RequestMethod.POST, "https", "api.envoyproxy.io", "/foo")
    .addRetryPolicy(RetryPolicy(...))
    .addUpstreamHttpProtocol(UpstreamRequestProtocol.HTTP2)
    .add("x-custom-header", "foobar")
    ...
    .build()

**Swift**::

  let headers = RequestHeadersBuilder(method: .post, scheme: "https", authority: "api.envoyproxy.io", path: "/foo")
    .addRetryPolicy(RetryPolicy(...))
    .addUpstreamHttpProtocol(.http2)
    .add(name: "x-custom-header", value: "foobar")
    ...
    .build()

-------------------
``StreamPrototype``
-------------------

A ``StreamPrototype`` is used to configure streams prior to starting them by assigning callbacks
to be invoked when response data is received on the stream.

To create a ``StreamPrototype``, use an instance of ``StreamClient``.

**Kotlin**::

  val prototype = streamClient
    .newStreamPrototype()
    .setOnResponseHeaders { headers, endStream ->
      Log.d("MainActivity", "[${headers.httpStatus}] Headers received: $headers, end stream: $endStream")
    }
    .setOnResponseData { data, endStream ->
      Log.d("MainActivity", "Received data, end stream: $endStream")
    }
    .setOnResponseTrailers { trailers ->
      Log.d("MainActivity", "Trailers received: $trailers")
    }
    .setOnError { ... }
    .setOnCancel { ... }

**Swift**::

  let prototype = streamClient
    .newStreamPrototype()
    .setOnResponseHeaders { headers, endStream in
      print("[\(headers.httpStatus)] Headers received: \(headers), end stream: \(endStream)")
    }
    .setOnResponseData { data, endStream in
      print("Received data, end stream: \(endStream)")
    }
    .setOnResponseTrailers { trailers in
      print("Trailers received: \(trailers)")
    }
    .setOnError { ... }
    .setOnCancel { ... }

---------------
``RetryPolicy``
---------------

The ``RetryPolicy`` type allows for customizing retry rules that should be applied to an outbound
request. These rules are added by calling ``addRetryPolicy(...)`` on the ``RequestHeadersBuilder``,
and are applied when the request headers are sent.

For full documentation of how these retry rules perform, see Envoy's documentation:

- `Automatic retries <https://www.envoyproxy.io/learn/automatic-retries>`_
- `Retry semantics <https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/http/http_routing.html?highlight=exponential#retry-semantics>`_

----------
``Stream``
----------

Streams are started by calling ``start()`` on a ``StreamPrototype``.

Doing so returns a ``Stream`` which allows the sender to interact with the stream.

**Kotlin**::

  val streamClient = AndroidStreamClientBuilder()
    ...
    .build()

  val requestHeaders = RequestHeadersBuilder()
    ...
    .build()
  val prototype = streamClient
    .newStreamPrototype()
    ...
  val stream = prototype
    .start(Executors.newSingleThreadExecutor())
    .sendHeaders(...)
    .sendData(...)

  ...
  stream.close(...)

**Swift**::

  let streamClient = StreamClientBuilder()
    ...
    .build()

  let requestHeaders = RequestHeadersBuilder()
    ...
    .build()
  let prototype = streamClient
    .newStreamPrototype()
    ...
  let stream = prototype
    .start(queue: .main)
    .sendHeaders(...)
    .sendData(...)

  ...
  stream.close(...)

--------------
Unary requests
--------------

As mentioned above, unary requests are made using the same types that handle streams.

Sending a unary request is done simply by closing the ``Stream`` after the
set of headers/data/trailers has been written.

For example:

**Kotlin**::

  val streamClient = AndroidStreamClientBuilder()
    ...
    .build()

  val requestHeaders = RequestHeadersBuilder()
    ...
    .build()
  val stream = streamClient
    .newStreamPrototype()
    .start(Executors.newSingleThreadExecutor())

  // Headers-only
  stream.sendHeaders(requestHeaders, true)

  // Close with data
  stream.close(ByteBuffer(...))

  // Close with trailers
  stream.close(RequestTrailersBuilder().build())

  // Cancel the stream
  stream.cancel()

**Swift**::

  let streamClient = StreamClientBuilder()
    ...
    .build()

  let requestHeaders = RequestHeadersBuilder()
    ...
    .build()
  let stream = streamClient
    .newStreamPrototype()
    .start(queue: .main)

  // Headers-only
  stream.sendHeaders(requestHeaders, endStream: true)

  // Close with data
  stream.close(Data(...))

  // Close with trailers
  stream.close(RequestTrailersBuilder().build())

  // Cancel the stream
  stream.cancel()
