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
and response flows. `LuaJIT <http://luajit.org/>`_ is used as the runtime. Because of this, the
supported Lua version is mostly 5.1 with some 5.2 features. See the `LuaJIT documentation
<http://luajit.org/extensions.html>`_ for more details.

The filter only supports loading Lua code in-line in the configuration. If local filesystem code
is desired, a trivial in-line script can be used to load the rest of the code from the local
environment.

The design of the filter and Lua support at a high level is as follows:

* All Lua environments are :ref:`per worker thread <arch_overview_threading>`. This means that
  there is no truly global data. Any globals create and populated at load time will be visible
  from each worker thread in isolation. True global support may be added via an API in the future.
* All scripts are run as coroutines. This means that they are written in a synchronous style even
  though they may perform complex asynchronous tasks. This makes the scripts substantially easier
  to write. All network/async processing is performed by Envoy via a set of APIs. Envoy will
  yield the script as appropriate and resume it when async tasks are complete.
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

* :ref:`v1 API reference <config_http_filters_lua_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.lua.v2.Lua>`

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
    -- Wait for the entire response body and a response header with the the body size.
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
      [":authority"] = "lua_cluster"
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

.. _config_http_filters_lua_stream_handle_api:

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
run *envoy_on_request* as a coroutine, passing an API handle. During the response path, Envoy will
run *envoy_on_response* as a coroutine, passing an API handle.

.. attention::

  It is critical that all interaction with Envoy occur through the passed stream handle. The stream
  handle should not be assigned to any global variable and should not be used outside of the
  coroutine. Envoy will fail your script if the handle is used incorrectly.

The following methods on the stream handle are supported:

headers()
^^^^^^^^^

.. code-block:: lua

  headers = handle:headers()

Returns the stream's headers. The headers can be modified as long as they have not been sent to
the next filter in the header chain. For example, they can be modified after an *httpCall()* or
after a *body()* call returns. The script will fail if the headers are modified in any other
situation.

Returns a :ref:`header object <config_http_filters_lua_header_wrapper>`.

body()
^^^^^^

.. code-block:: lua

  body = handle:body()

Returns the stream's body. This call will cause Envoy to yield the script until the entire body
has been buffered. Note that all buffering must adhere to the flow control policies in place.
Envoy will not buffer more data than is allowed by the connection manager.

Returns a :ref:`buffer object <config_http_filters_lua_buffer_wrapper>`.

bodyChunks()
^^^^^^^^^^^^

.. code-block:: lua

  iterator = handle:bodyChunks()

Returns an iterator that can be used to iterate through all received body chunks as they arrive.
Envoy will yield the script in between chunks, but *will not buffer* them. This can be used by
a script to inspect data as it is streaming by.

.. code-block:: lua

  for chunk in request_handle:bodyChunks() do
    request_handle:log(0, chunk:length())
  end

Each chunk the iterator returns is a :ref:`buffer object <config_http_filters_lua_buffer_wrapper>`.

trailers()
^^^^^^^^^^

.. code-block:: lua

  trailers = handle:trailers()

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

  headers, body = handle:httpCall(cluster, headers, body, timeout)

Makes an HTTP call to an upstream host. Envoy will yield the script until the call completes or
has an error. *cluster* is a string which maps to a configured cluster manager cluster. *headers*
is a table of key/value pairs to send. Note that the *:method*, *:path*, and *:authority* headers
must be set. *body* is an optional string of body data to send. *timeout* is an integer that
specifies the call timeout in milliseconds.

Returns *headers* which is a table of response headers. Returns *body* which is the string response
body. May be nil if there is no body.

respond()
^^^^^^^^^^

.. code-block:: lua

  handle:respond(headers, body)

Respond immediately and do not continue further filter iteration. This call is *only valid in
the request flow*. Additionally, a response is only possible if request headers have not yet been
passed to subsequent filters. Meaning, the following Lua code is invalid:

.. code-block:: lua

  function envoy_on_request(request_handle)
    for chunk in request_handle:bodyChunks() do
      request_handle:respond(
        {[":status"] = "100"},
        "nope")
    end
  end

*headers* is a table of key/value pairs to send. Note that the *:status* header
must be set. *body* is a string and supplies the optional response body. May be nil.

metadata()
^^^^^^^^^^

.. code-block:: lua

  metadata = handle:metadata()

Returns the current route entry metadata. Note that the metadata should be specified
under the filter name i.e. *envoy.lua*. Below is an example of a *metadata* in a
:ref:`route entry <config_http_conn_man_route_table_route>`.

.. code-block:: yaml

  metadata:
    filter_metadata:
      envoy.lua:
        foo: bar
        baz:
          - bad
          - baz

Returns a :ref:`metadata object <config_http_filters_lua_metadata_wrapper>`.

.. _config_http_filters_lua_header_wrapper:

Header object API
-----------------

add()
^^^^^

.. code-block:: lua

  headers:add(key, value)

Adds a header. *key* is a string that supplies the header key. *value* is a string that supplies
the header value.

.. attention::

  Envoy treats certain headers specially. These are known as the O(1) or *inline* headers. The
  list of inline headers can be found `here <https://github.com/envoyproxy/envoy/blob/6b6ce85a24094146c5c225f19b6ecc47b2ca84bf/include/envoy/http/header_map.h#L228>`_.
  If an inline header is already present in the header map, *add()* will have no effect. If
  attempting to *add()* a non-inline header, the additional header will be added so that the
  resultant headers contains multiple header entries with the same name. Consider using the
  *replace* function if want to replace a header with another value. Note also that we
  understand this behavior is confusing and we may change it in a future release.

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

  In the currently implementation, headers cannot be modified during iteration. Additionally, if
  it is desired to modify headers after iteration, the iteration must be completed. Meaning, do
  not use `break` or any other mechanism to exit the loop early. This may be relaxed in the future.

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

  size = buffer:length()

Gets the size of the buffer in bytes. Returns an integer.

getBytes()
^^^^^^^^^^

.. code-block:: lua

  buffer:getBytes(index, length)

Get bytes from the buffer. By default Envoy will not copy all buffer bytes to Lua. This will
cause a buffer segment to be copied. *index* is an integer and supplies the buffer start index to
copy. *length* is an integer and supplies the buffer length to copy. *index* + *length* must be
less than the buffer length.

.. _config_http_filters_lua_metadata_wrapper:

Metadata object API
-------------------

get()
^^^^^

.. code-block:: lua

  metadata:get(key)

Gets a metadata. *key* is a string that supplies the metadata key. Returns the corresponding
value of the given metadata key. The type of the value can be: *null*, *boolean*, *number*,
*string* and *table*.

__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(metadata) do
  end

Iterates through every *metadata* entry. *key* is a string that supplies a *metadata*
key. *value* is *metadata* entry value.
