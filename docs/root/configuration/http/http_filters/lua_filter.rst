.. _config_http_filters_lua:

Lua
===

.. attention::

  By default Envoy is built without exporting symbols that you may need when interacting with Lua
  modules installed as shared objects. Envoy may need to be built with support for exported symbols.
  Please see the :repo:`Bazel docs <bazel/README.md>` for more information.

Overview
--------

The HTTP Lua filter allows `Lua <https://www.lua.org/>`_ scripts to be run during both the request
and response flows. `LuaJIT <https://luajit.org/>`_ is used as the runtime. Because of this, the
supported Lua version is mostly 5.1 with some 5.2 features. See the `LuaJIT documentation
<https://luajit.org/extensions.html>`_ for more details.

.. note::

  `moonjit <https://github.com/moonjit/moonjit/>`_ is a continuation of LuaJIT development, which
  supports more 5.2 features and additional architectures. Envoy can be built with moonjit support
  by using the following bazel option: ``--//source/extensions/filters/common/lua:moonjit=1``.

The design of the filter and Lua support at a high level is as follows:

* All Lua environments are :ref:`per worker thread <arch_overview_threading>`. This means that
  there is no truly global data. Any globals created and populated at load time will be visible
  from each worker thread in isolation. True global support may be added via an API in the future.
* All scripts are run as coroutines. This means that they are written in a synchronous style even
  though they may perform complex asynchronous tasks. This makes the scripts substantially easier
  to write. All network/async processing is performed by Envoy via a set of APIs. Envoy will
  suspend execution of the script as appropriate and resume it when async tasks are complete.
* **Do not perform blocking operations from scripts.** It is critical for performance that
  Envoy APIs are used for all IO.

Currently supported high level features
---------------------------------------

**NOTE:** It is expected that this list will expand over time as the filter is used in production.
The API surface has been kept small on purpose. The goal is to make scripts extremely simple and
safe to write. Very complex or high performance use cases are assumed to use the native C++ filter
API.

* Inspection of headers, body, and trailers while streaming in either the request flow, response
  flow, or both.
* Modification of headers and trailers.
* Blocking and buffering the full request/response body for inspection.
* Performing an outbound async HTTP call to an upstream host. Such a call can be performed while
  buffering body data so that when the call completes upstream headers can be modified.
* Performing a direct response and skipping further filter iteration. For example, a script
  could make an upstream HTTP call for authentication, and then directly respond with a 403
  response code.

Configuration
-------------

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.lua.v3.Lua>`
* This filter should be configured with the name *envoy.filters.http.lua*.

A simple example of configuring Lua HTTP filter that contains only :ref:`inline_code
<envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.inline_code>` is as follow:

.. code-block:: yaml

  name: envoy.filters.http.lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    inline_code: |
      -- Called on the request path.
      function envoy_on_request(request_handle)
        -- Do something.
      end
      -- Called on the response path.
      function envoy_on_response(response_handle)
        -- Do something.
      end

By default, Lua script defined in ``inline_code`` will be treated as a ``GLOBAL`` script. Envoy will
execute it for every HTTP request.

Per-Route Configuration
-----------------------

The Lua HTTP filter also can be disabled or overridden on a per-route basis by providing a
:ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` configuration
on the virtual host, route, or weighted cluster.

LuaPerRoute provides two ways of overriding the `GLOBAL` Lua script:

* By providing a name reference to the defined :ref:`named Lua source codes map
  <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.source_codes>`.
* By providing inline :ref:`source code
  <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.source_code>` (This allows the
  code to be sent through RDS).

As a concrete example, given the following Lua filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    inline_code: |
      function envoy_on_request(request_handle)
        -- do something
      end
    source_codes:
      hello.lua:
        inline_string: |
          function envoy_on_request(request_handle)
            request_handle:logInfo("Hello World.")
          end
      bye.lua:
        inline_string: |
          function envoy_on_response(response_handle)
            response_handle:logInfo("Bye Bye.")
          end

The HTTP Lua filter can be disabled on some virtual host, route, or weighted cluster by the
:ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` configuration as
follow:

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      disabled: true

We can also refer to a Lua script in the filter configuration by specifying a name in LuaPerRoute.
The ``GLOBAL`` Lua script will be overridden by the referenced script:

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      name: hello.lua

.. attention::

  The name ``GLOBAL`` is reserved for :ref:`Lua.inline_code
  <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.inline_code>`. Therefore, do not use
  ``GLOBAL`` as name for other Lua scripts.

Or we can define a new Lua script in the LuaPerRoute configuration directly to override the `GLOBAL`
Lua script as follows:

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      source_code:
        inline_string: |
          function envoy_on_response(response_handle)
            response_handle:logInfo("Goodbye.")
          end


Script examples
---------------

This section provides some concrete examples of Lua scripts as a more gentle introduction and quick
start. Please refer to the :ref:`stream handle API <config_http_filters_lua_stream_handle_api>` for
more details on the supported API.

.. code-block:: lua

  -- Called on the request path.
  function envoy_on_request(request_handle)
    -- Wait for the entire request body and add a request header with the body size.
    request_handle:headers():add("request_body_size", request_handle:body():length())
  end

  -- Called on the response path.
  function envoy_on_response(response_handle)
    -- Wait for the entire response body and add a response header with the body size.
    response_handle:headers():add("response_body_size", response_handle:body():length())
    -- Remove a response header named 'foo'
    response_handle:headers():remove("foo")
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Make an HTTP call to an upstream host with the following headers, body, and timeout.
    local headers, body = request_handle:httpCall(
    "lua_cluster",
    {
      [":method"] = "POST",
      [":path"] = "/",
      [":authority"] = "lua_cluster"
    },
    "hello world",
    5000)

    -- Add information from the HTTP call into the headers that are about to be sent to the next
    -- filter in the filter chain.
    request_handle:headers():add("upstream_foo", headers["foo"])
    request_handle:headers():add("upstream_body_size", #body)
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Make an HTTP call.
    local headers, body = request_handle:httpCall(
    "lua_cluster",
    {
      [":method"] = "POST",
      [":path"] = "/",
      [":authority"] = "lua_cluster",
      ["set-cookie"] = { "lang=lua; Path=/", "type=binding; Path=/" }
    },
    "hello world",
    5000)

    -- Response directly and set a header from the HTTP call. No further filter iteration
    -- occurs.
    request_handle:respond(
      {[":status"] = "403",
       ["upstream_foo"] = headers["foo"]},
      "nope")
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Log information about the request
    request_handle:logInfo("Authority: "..request_handle:headers():get(":authority"))
    request_handle:logInfo("Method: "..request_handle:headers():get(":method"))
    request_handle:logInfo("Path: "..request_handle:headers():get(":path"))
  end

  function envoy_on_response(response_handle)
    -- Log response status code
    response_handle:logInfo("Status: "..response_handle:headers():get(":status"))
  end

A common use-case is to rewrite upstream response body, for example: an upstream sends non-2xx
response with JSON data, but the application requires HTML page to be sent to browsers.

There are two ways of doing this, the first one is via the `body()` API.

.. code-block:: lua

    function envoy_on_response(response_handle)
      local content_length = response_handle:body():setBytes("<html><b>Not Found<b></html>")
      response_handle:headers():replace("content-length", content_length)
      response_handle:headers():replace("content-type", "text/html")
    end


Or, through `bodyChunks()` API, which let Envoy to skip buffering the upstream response data.

.. code-block:: lua

    function envoy_on_response(response_handle)

      -- Sets the content-length.
      response_handle:headers():replace("content-length", 28)
      response_handle:headers():replace("content-type", "text/html")

      local last
      for chunk in response_handle:bodyChunks() do
        -- Clears each received chunk.
        chunk:setBytes("")
        last = chunk
      end

      last:setBytes("<html><b>Not Found<b></html>")
    end

.. _config_http_filters_lua_stream_handle_api:

Complete example
----------------

A complete example using Docker is available in :repo:`/examples/lua`.

Stream handle API
-----------------

When Envoy loads the script in the configuration, it looks for two global functions that the
script defines:

.. code-block:: lua

  function envoy_on_request(request_handle)
  end

  function envoy_on_response(response_handle)
  end

A script can define either or both of these functions. During the request path, Envoy will
run *envoy_on_request* as a coroutine, passing a handle to the request API. During the
response path, Envoy will run *envoy_on_response* as a coroutine, passing handle to the
response API.

.. attention::

  It is critical that all interaction with Envoy occur through the passed stream handle. The stream
  handle should not be assigned to any global variable and should not be used outside of the
  coroutine. Envoy will fail your script if the handle is used incorrectly.

The following methods on the stream handle are supported:

headers()
^^^^^^^^^

.. code-block:: lua

  local headers = handle:headers()

Returns the stream's headers. The headers can be modified as long as they have not been sent to
the next filter in the header chain. For example, they can be modified after an *httpCall()* or
after a *body()* call returns. The script will fail if the headers are modified in any other
situation.

Returns a :ref:`header object <config_http_filters_lua_header_wrapper>`.

body()
^^^^^^

.. code-block:: lua

  local body = handle:body()

Returns the stream's body. This call will cause Envoy to suspend execution of the script until
the entire body has been received in a buffer. Note that all buffering must adhere to the
flow-control policies in place. Envoy will not buffer more data than is allowed by the connection
manager.

Returns a :ref:`buffer object <config_http_filters_lua_buffer_wrapper>`.

bodyChunks()
^^^^^^^^^^^^

.. code-block:: lua

  local iterator = handle:bodyChunks()

Returns an iterator that can be used to iterate through all received body chunks as they arrive.
Envoy will suspend executing the script in between chunks, but *will not buffer* them. This can be
used by a script to inspect data as it is streaming by.

.. code-block:: lua

  for chunk in request_handle:bodyChunks() do
    request_handle:log(0, chunk:length())
  end

Each chunk the iterator returns is a :ref:`buffer object <config_http_filters_lua_buffer_wrapper>`.

trailers()
^^^^^^^^^^

.. code-block:: lua

  local trailers = handle:trailers()

Returns the stream's trailers. May return nil if there are no trailers. The trailers may be
modified before they are sent to the next filter.

Returns a :ref:`header object <config_http_filters_lua_header_wrapper>`.

log*()
^^^^^^

.. code-block:: lua

  handle:logTrace(message)
  handle:logDebug(message)
  handle:logInfo(message)
  handle:logWarn(message)
  handle:logErr(message)
  handle:logCritical(message)

Logs a message using Envoy's application logging. *message* is a string to log.

httpCall()
^^^^^^^^^^

.. code-block:: lua

  local headers, body = handle:httpCall(cluster, headers, body, timeout, asynchronous)

Makes an HTTP call to an upstream host. *cluster* is a string which maps to a configured cluster manager cluster. *headers*
is a table of key/value pairs to send (the value can be a string or table of strings). Note that
the *:method*, *:path*, and *:authority* headers must be set. *body* is an optional string of body
data to send. *timeout* is an integer that specifies the call timeout in milliseconds.

*asynchronous* is a boolean flag. If asynchronous is set to true, Envoy will make the HTTP request and continue,
regardless of the response success or failure. If this is set to false, or not set, Envoy will suspend executing the script
until the call completes or has an error.

Returns *headers* which is a table of response headers. Returns *body* which is the string response
body. May be nil if there is no body.

respond()
^^^^^^^^^^

.. code-block:: lua

  handle:respond(headers, body)

Respond immediately and do not continue further filter iteration. This call is *only valid in
the request flow*. Additionally, a response is only possible if the request headers have not yet been
passed to subsequent filters. Meaning, the following Lua code is invalid:

.. code-block:: lua

  function envoy_on_request(request_handle)
    for chunk in request_handle:bodyChunks() do
      request_handle:respond(
        {[":status"] = "100"},
        "nope")
    end
  end

*headers* is a table of key/value pairs to send (the value can be a string or table of strings).
Note that the *:status* header must be set. *body* is a string and supplies the optional response
body. May be nil.

metadata()
^^^^^^^^^^

.. code-block:: lua

  local metadata = handle:metadata()

Returns the current route entry metadata. Note that the metadata should be specified
under the filter name i.e. *envoy.filters.http.lua*. Below is an example of a *metadata* in a
:ref:`route entry <envoy_v3_api_msg_config.route.v3.Route>`.

.. code-block:: yaml

  metadata:
    filter_metadata:
      envoy.filters.http.lua:
        foo: bar
        baz:
          - bad
          - baz

Returns a :ref:`metadata object <config_http_filters_lua_metadata_wrapper>`.

streamInfo()
^^^^^^^^^^^^^

.. code-block:: lua

  local streamInfo = handle:streamInfo()

Returns :repo:`information <include/envoy/stream_info/stream_info.h>` related to the current request.

Returns a :ref:`stream info object <config_http_filters_lua_stream_info_wrapper>`.

connection()
^^^^^^^^^^^^

.. code-block:: lua

  local connection = handle:connection()

Returns the current request's underlying :repo:`connection <include/envoy/network/connection.h>`.

Returns a :ref:`connection object <config_http_filters_lua_connection_wrapper>`.

importPublicKey()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local pubkey = handle:importPublicKey(keyder, keyderLength)

Returns public key which is used by :ref:`verifySignature <verify_signature>` to verify digital signature.

.. _verify_signature:

verifySignature()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local ok, error = verifySignature(hashFunction, pubkey, signature, signatureLength, data, dataLength)

Verify signature using provided parameters. *hashFunction* is the variable for the hash function which be used
for verifying signature. *SHA1*, *SHA224*, *SHA256*, *SHA384* and *SHA512* are supported.
*pubkey* is the public key. *signature* is the signature to be verified. *signatureLength* is
the length of the signature. *data* is the content which will be hashed. *dataLength* is the length of data.

The function returns a pair. If the first element is *true*, the second element will be empty
which means signature is verified; otherwise, the second element will store the error message.

.. _config_http_filters_lua_stream_handle_api_base64_escape:

base64Escape()
^^^^^^^^^^^^^^
.. code-block:: lua

  local base64_encoded = handle:base64Escape("input string")

Encodes the input string as base64. This can be useful for escaping binary data.

.. _config_http_filters_lua_header_wrapper:

Header object API
-----------------

add()
^^^^^

.. code-block:: lua

  headers:add(key, value)

Adds a header. *key* is a string that supplies the header key. *value* is a string that supplies
the header value.

get()
^^^^^

.. code-block:: lua

  headers:get(key)

Gets a header. *key* is a string that supplies the header key. Returns a string that is the header
value or nil if there is no such header.

__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(headers) do
  end

Iterates through every header. *key* is a string that supplies the header key. *value* is a string
that supplies the header value.

.. attention::

  In the current implementation, headers cannot be modified during iteration. Additionally, if
  it is necessary to modify headers after an iteration, the iteration must first be completed. This means that
  `break` or any other way to exit the loop early must not be used. This may be more flexible in the future.

remove()
^^^^^^^^

.. code-block:: lua

  headers:remove(key)

Removes a header. *key* supplies the header key to remove.

replace()
^^^^^^^^^

.. code-block:: lua

  headers:replace(key, value)

Replaces a header. *key* is a string that supplies the header key. *value* is a string that supplies
the header value. If the header does not exist, it is added as per the *add()* function.

.. _config_http_filters_lua_buffer_wrapper:

Buffer API
----------

length()
^^^^^^^^^^

.. code-block:: lua

  local size = buffer:length()

Gets the size of the buffer in bytes. Returns an integer.

getBytes()
^^^^^^^^^^

.. code-block:: lua

  buffer:getBytes(index, length)

Get bytes from the buffer. By default Envoy will not copy all buffer bytes to Lua. This will
cause a buffer segment to be copied. *index* is an integer and supplies the buffer start index to
copy. *length* is an integer and supplies the buffer length to copy. *index* + *length* must be
less than the buffer length.

.. _config_http_filters_lua_buffer_wrapper_api_set_bytes:

setBytes()
^^^^^^^^^^

.. code-block:: lua

  buffer:setBytes(string)

Set the content of wrapped buffer with the input string.

.. _config_http_filters_lua_metadata_wrapper:

Metadata object API
-------------------

get()
^^^^^

.. code-block:: lua

  metadata:get(key)

Gets a metadata. *key* is a string that supplies the metadata key. Returns the corresponding
value of the given metadata key. The type of the value can be: *nil*, *boolean*, *number*,
*string* and *table*.

__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(metadata) do
  end

Iterates through every *metadata* entry. *key* is a string that supplies a *metadata*
key. *value* is a *metadata* entry value.

.. _config_http_filters_lua_stream_info_wrapper:

Stream info object API
-----------------------

protocol()
^^^^^^^^^^

.. code-block:: lua

  streamInfo:protocol()

Returns the string representation of :repo:`HTTP protocol <include/envoy/http/protocol.h>`
used by the current request. The possible values are: *HTTP/1.0*, *HTTP/1.1*, and *HTTP/2*.

downstreamLocalAddress()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamLocalAddress()

Returns the string representation of :repo:`downstream remote address <include/envoy/stream_info/stream_info.h>`
used by the current request.

downstreamDirectRemoteAddress()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamDirectRemoteAddress()

Returns the string representation of :repo:`downstream directly connected address <include/envoy/stream_info/stream_info.h>`
used by the current request. This is equivalent to the address of the physical connection.

dynamicMetadata()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:dynamicMetadata()

Returns a :ref:`dynamic metadata object <config_http_filters_lua_stream_info_dynamic_metadata_wrapper>`.

downstreamSslConnection()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamSslConnection()

Returns :repo:`information <include/envoy/ssl/connection.h>` related to the current SSL connection.

Returns a downstream :ref:`SSL connection info object <config_http_filters_lua_ssl_socket_info>`.

.. _config_http_filters_lua_stream_info_dynamic_metadata_wrapper:

Dynamic metadata object API
---------------------------

get()
^^^^^

.. code-block:: lua

  dynamicMetadata:get(filterName)

  -- to get a value from a returned table.
  dynamicMetadata:get(filterName)[key]

Gets an entry in dynamic metadata struct. *filterName* is a string that supplies the filter name, e.g. *envoy.lb*.
Returns the corresponding *table* of a given *filterName*.

set()
^^^^^

.. code-block:: lua

  dynamicMetadata:set(filterName, key, value)

Sets key-value pair of a *filterName*'s metadata. *filterName* is a key specifying the target filter name,
e.g. *envoy.lb*. The type of *key* is *string*. The type of *value* is any Lua type that can be mapped
to a metadata value: *table*, *numeric*, *boolean*, *string* or *nil*. When using a *table* as an argument,
its keys can only be *string* or *numeric*.

.. code-block:: lua

  function envoy_on_request(request_handle)
    local headers = request_handle:headers()
    request_handle:streamInfo():dynamicMetadata():set("envoy.filters.http.lua", "request.info", {
      auth: headers:get("authorization"),
      token: headers:get("x-request-token"),
    })
  end

  function envoy_on_response(response_handle)
    local meta = response_handle:streamInfo():dynamicMetadata():get("envoy.filters.http.lua")["request.info"]
    response_handle:logInfo("Auth: "..meta.auth..", token: "..meta.token)
  end


__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(dynamicMetadata) do
  end

Iterates through every *dynamicMetadata* entry. *key* is a string that supplies a *dynamicMetadata*
key. *value* is a *dynamicMetadata* entry value.

.. _config_http_filters_lua_connection_wrapper:

Connection object API
---------------------

ssl()
^^^^^

.. code-block:: lua

  if connection:ssl() == nil then
    print("plain")
  else
    print("secure")
  end

Returns :repo:`SSL connection <include/envoy/ssl/connection.h>` object when the connection is
secured and *nil* when it is not.

Returns an :ref:`SSL connection info object <config_http_filters_lua_ssl_socket_info>`.

.. _config_http_filters_lua_ssl_socket_info:

SSL connection object API
-------------------------

peerCertificatePresented()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificatePresented() then
    print("peer certificate is presented")
  end

Returns a bool representing whether the peer certificate is presented.

peerCertificateValidated()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificateVaidated() then
    print("peer certificate is valiedated")
  end

Returns bool whether the peer certificate was validated.

uriSanLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  -- For example, uriSanLocalCertificate contains {"san1", "san2"}
  local certs = downstreamSslConnection:uriSanLocalCertificate()

  -- The following prints san1,san2
  handle:logTrace(table.concat(certs, ","))

Returns the URIs (as a table) in the SAN field of the local certificate. Returns an empty table if
there is no local certificate, or no SAN field, or no URI SAN entries.

sha256PeerCertificateDigest()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sha256PeerCertificateDigest()

Returns the SHA256 digest of the peer certificate. Returns ``""`` if there is no peer certificate
which can happen in TLS (non-mTLS) connections.

serialNumberPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:serialNumberPeerCertificate()

Returns the serial number field of the peer certificate. Returns ``""`` if there is no peer
certificate, or no serial number.

issuerPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:issuerPeerCertificate()

Returns the issuer field of the peer certificate in RFC 2253 format. Returns ``""`` if there is no
peer certificate, or no issuer.

subjectPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectPeerCertificate()

Return the subject field of the peer certificate in RFC 2253 format. Returns ``""`` if there is no
peer certificate, or no subject.

uriSanPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:uriSanPeerCertificate()

Returns the URIs (as a table) in the SAN field of the peer certificate. Returns an empty table if
there is no peer certificate, or no SAN field, or no URI SAN entries.

subjectLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectLocalCertificate()

Returns the subject field of the local certificate in RFC 2253 format. Returns ``""`` if there is no
local certificate, or no subject.

urlEncodedPemEncodedPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificate()

Returns the URL-encoded PEM-encoded representation of the peer certificate. Returns ``""`` if there
is no peer certificate or encoding fails.

urlEncodedPemEncodedPeerCertificateChain()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificateChain()

Returnns the URL-encoded PEM-encoded representation of the full peer certificate chain including the
leaf certificate. Returns ``""`` if there is no peer certificate or encoding fails.

dnsSansPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansPeerCertificate()

Returns the DNS entries (as a table) in the SAN field of the peer certificate. Returns an empty
table if there is no peer certificate, or no SAN field, or no DNS SAN entries.

dnsSansLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansLocalCertificate()

Returns the DNS entries (as a table) in the SAN field of the local certificate. Returns an empty
table if there is no local certificate, or no SAN field, or no DNS SAN entries.

validFromPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

Returns the time (timestamp-since-epoch in seconds) that the peer certificate was issued and should
be considered valid from. Returns ``0`` if there is no peer certificate.

In Lua, we usually use ``os.time(os.date("!*t"))`` to get current timestamp-since-epoch in seconds.

expirationPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

Returns the time (timestamp-since-epoch in seconds) that the peer certificate expires and should not
be considered valid after. Returns ``0`` if there is no peer certificate.

In Lua, we usually use ``os.time(os.date("!*t"))`` to get current timestamp-since-epoch in seconds.

sessionId()
^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sessionId()

Returns the hex-encoded TLS session ID as defined in RFC 5246.

ciphersuiteId()
^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteId()

Returns the standard ID (hex-encoded) for the ciphers used in the established TLS connection.
Returns ``"0xffff"`` if there is no current negotiated ciphersuite.

ciphersuiteString()
^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteString()

Returns the OpenSSL name for the set of ciphers used in the established TLS connection. Returns
``""`` if there is no current negotiated ciphersuite.

tlsVersion()
^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificateChain()

Returns the TLS version (e.g., TLSv1.2, TLSv1.3) used in the established TLS connection.
