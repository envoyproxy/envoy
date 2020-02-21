.. _api_http:

HTTP requests and streams
=========================

Streams are first-class citizens in Envoy Mobile, and are supported out-of-the-box.

In fact, unary (single request, single response) HTTP requests are actually written as simply
convenience functions on top of streams.

-----------
``Request``
-----------

Creating a stream is done by initializing a ``Request`` via a ``RequestBuilder``, then passing it to
a previously created :ref:`Envoy instance <api_starting_envoy>`.

**Kotlin**::

  val request = RequestBuilder(RequestMethod.POST, "https", "api.envoyproxy.io", "/foo")
    .addRetryPolicy(RetryPolicy(...))
    .addUpstreamHttpProtocol(UpstreamRequestProtocol.HTTP2)
    .addHeader("x-custom-header", "foobar")
    ...
    .build()

**Swift**::

  let request = RequestBuilder(method: .post, scheme: "https", authority: "api.envoyproxy.io", path: "/foo")
    .addRetryPolicy(RetryPolicy(...))
    .addUpstreamHttpProtocol(.http2)
    .addHeader(name: "x-custom-header", value: "foobar")
    ...
    .build()

-------------------
``ResponseHandler``
-------------------

In order to receive updates for a given request/stream, a ``ResponseHandler`` must be created.
This class contains a set of callbacks that are called whenever an update occurs on the stream.

**Kotlin**::

  val handler = ResponseHandler(Executor {})
    .onHeaders { headers, statusCode, _ ->
      Log.d("MainActivity", "Received status: " + statusCode + " and headers: " + headers)
      Unit
    }
    .onData { buffer, endStream ->
      if (endStream) {
        Log.d("MainActivity", "Finished receiving body data")
      } else {
        Log.d("MainActivity", "Received body data, more incoming")
      }
      Unit
    }
    .onError { error ->
      Log.d("MainActivity", "Error received: " + error.message)
      Unit
    }
    ...

**Swift**::

  let handler = ResponseHandler()
    .onHeaders { headers, statusCode, _ in
      print("Received status: \(statusCode) and headers: \(headers)")
    }
    .onData { buffer, endStream in
      if endStream {
        print("Finished receiving body data")
      } else {
        print("Received body data, more incoming")
      }
    }
    .onError { error in
      print("Error received: \(error.message)")
    }
    ...

---------------
``RetryPolicy``
---------------

The ``RetryPolicy`` type allows for customizing retry rules that should be applied to an outbound
request. These rules are added by calling ``addRetryPolicy(...)`` on the ``RequestBuilder``, and
are applied when the request is sent.

For full documentation of how these retry rules perform, see Envoy's documentation:

- `Automatic retries <https://www.envoyproxy.io/learn/automatic-retries>`_
- `Retry semantics <https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/http/http_routing.html?highlight=exponential#retry-semantics>`_

-----------------
``StreamEmitter``
-----------------

Once a ``Request`` and ``ResponseHandler`` have been created, a stream can be opened using an
:ref:`Envoy instance <api_starting_envoy>`.

Doing so returns a ``StreamEmitter`` which allows the sender to interact with the stream.

**Kotlin**::

  val envoy = AndroidEnvoyClientBuilder(...).build()

  val request = RequestBuilder(...).build()
  val responseHandler = ResponseHandler(...)
  val emitter = envoy.send(request, responseHandler)
    .sendData(...)
    .sendData(...)

  ...
  emitter.close(...)

**Swift**::

  let envoy = try EnvoyClientBuilder(...).build()

  let request = RequestBuilder(...).build()
  let responseHandler = ResponseHandler(...)
  let emitter = envoy.send(request, handler: responseHandler)
    .sendData(...)
    .sendData(...)

  ...
  emitter.close(...)

--------------
Unary Requests
--------------

As mentioned above, unary requests are made using the same types that perform streaming requests.

Sending a unary request may be done by either closing the ``StreamEmitter`` after the
set of headers/data has been written, or by using the helper function that returns a
``CancelableStream`` type instead of a ``StreamEmitter``.

The unary helper function takes optional body data, then closes the stream.
The ``CancelableStream`` it returns does not expose options for sending additional data.

**Kotlin**::

  val envoy = AndroidEnvoyClientBuilder(...).build()

  val request = RequestBuilder(...).build()
  val responseHandler = ResponseHandler(...)
  val cancelable = envoy.send(request, body, trailers, responseHandler)
  // cancelable.cancel()

**Swift**::

  let envoy = try EnvoyClientBuilder(...).build()

  let request = RequestBuilder(...).build()
  let responseHandler = ResponseHandler(...)
  let cancelable = envoy.send(request, body, trailers: [:], handler: responseHandler)
  // cancelable.cancel()
