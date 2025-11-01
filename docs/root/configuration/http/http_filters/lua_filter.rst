.. _config_http_filters_lua:

Lua
===

Overview
--------

The HTTP Lua filter allows `Lua <https://www.lua.org/>`_ scripts to be run during both the request
and response flows. `LuaJIT <https://luajit.org/>`_ is used as the runtime. Because of this, the
supported Lua version is mostly 5.1 with some 5.2 features. See the `LuaJIT documentation
<https://luajit.org/extensions.html>`_ for more details.

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

Currently supported high-level features
---------------------------------------

.. note::

  It is expected that this list will expand over time as the filter is used in production.
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

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.lua.v3.Lua>`

A simple example of configuring the Lua HTTP filter that contains only :ref:`default source code
<envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.default_source_code>` is as follows:

.. literalinclude:: _include/lua-filter.yaml
    :language: yaml
    :lines: 41-53
    :lineno-start: 41
    :linenos:
    :caption: :download:`lua-filter.yaml <_include/lua-filter.yaml>`

By default, the Lua script defined in ``default_source_code`` will be treated as a ``default`` script. Envoy will
execute it for every HTTP request. This ``default`` script is optional.

Per-Route Configuration
-----------------------

The Lua HTTP filter also can be disabled or overridden on a per-route basis by providing a
:ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` configuration
on the virtual host, route, or weighted cluster.

LuaPerRoute provides two ways of overriding the ``default`` Lua script:

* By providing a name reference to the defined :ref:`named Lua source codes map
  <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.source_codes>`.
* By providing inline :ref:`source code
  <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.source_code>` (this allows the
  code to be sent through RDS).

As a concrete example, given the following Lua filter configuration:

.. literalinclude:: _include/lua-filter-override.yaml
    :language: yaml
    :lines: 58-76
    :lineno-start: 58
    :linenos:
    :caption: :download:`lua-filter-override.yaml <_include/lua-filter-override.yaml>`

The HTTP Lua filter can be disabled on some virtual host, route, or weighted cluster by the
:ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` configuration as
follows:

.. literalinclude:: _include/lua-filter-override.yaml
    :language: yaml
    :lines: 31-34
    :lineno-start: 31
    :linenos:
    :caption: :download:`lua-filter-override.yaml <_include/lua-filter-override.yaml>`

We can also refer to a Lua script in the filter configuration by specifying a name in LuaPerRoute.
The ``default`` Lua script will be overridden by the referenced script:

.. literalinclude:: _include/lua-filter-override.yaml
    :language: yaml
    :lines: 40-43
    :lineno-start: 40
    :linenos:
    :caption: :download:`lua-filter-override.yaml <_include/lua-filter-override.yaml>`

Or we can define a new Lua script in the LuaPerRoute configuration directly to override the ``default``
Lua script as follows:

.. literalinclude:: _include/lua-filter-override.yaml
    :language: yaml
    :lines: 49-56
    :lineno-start: 49
    :linenos:
    :caption: :download:`lua-filter-override.yaml <_include/lua-filter-override.yaml>`

Upstream Filter
---------------

The Lua filter can be used as an upstream filter. Upstream filters cannot clear the route cache (as
the routing decision has already been made). Clearing the route cache will be a no-op in this case.

Statistics
----------
.. _config_http_filters_lua_stats:

The Lua filter outputs statistics in the ``.lua.`` namespace by default. When
there are multiple Lua filters configured in a filter chain, stats from
individual filter instance/script can be tracked by providing a per-filter
:ref:`stat prefix
<envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.stat_prefix>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  errors, Counter, Total script execution errors.
  executions, Counter, Total number of times ``envoy_on_request`` and ``envoy_on_response`` was executed.

Script examples
---------------

This section provides some concrete examples of Lua scripts as a gentler introduction and quick
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

A common use case is to rewrite the upstream response body. For example: an upstream sends a non-2xx
response with JSON data, but the application requires an HTML page to be sent to browsers.

There are two ways of doing this, the first one is via the ``body()`` API.

.. code-block:: lua

    function envoy_on_response(response_handle)
      response_handle:body():setBytes("<html><b>Not Found<b></html>")
      response_handle:headers():replace("content-type", "text/html")
    end


Or, through the ``bodyChunks()`` API, which lets Envoy skip buffering the upstream response data.

.. code-block:: lua

    function envoy_on_response(response_handle)

      -- Sets the content-type.
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

A complete example using Docker is available in `here <https://github.com/envoyproxy/examples/tree/main/lua>`__.

Stream handle API
-----------------

When Envoy loads the script in the configuration, it looks for two global functions defined by the
script:

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

``headers()``
^^^^^^^^^^^^^

.. code-block:: lua

  local headers = handle:headers()

Returns the stream's headers. The headers can be modified as long as they have not been sent to
the next filter in the filter chain. For example, they can be modified after an ``httpCall()`` or
after a ``body()`` call returns. The script will fail if the headers are modified in any other
situation.

Returns a :ref:`header object <config_http_filters_lua_header_wrapper>`.

``body()``
^^^^^^^^^^

.. code-block:: lua

  local body = handle:body(always_wrap_body)

Returns the stream's body. This call will cause Envoy to suspend execution of the script until
the entire body has been received in a buffer. Note that all buffering must adhere to the
flow-control policies in place. Envoy will not buffer more data than is allowed by the connection
manager.

An optional boolean argument ``always_wrap_body`` can be used to require that Envoy always returns a
``body`` object even if the body is empty. Therefore, we can modify the body regardless of whether the
original body exists or not.

Returns a :ref:`buffer object <config_http_filters_lua_buffer_wrapper>`.

``bodyChunks()``
^^^^^^^^^^^^^^^^

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

``trailers()``
^^^^^^^^^^^^^^

.. code-block:: lua

  local trailers = handle:trailers()

Returns the stream's trailers. Before calling this method, the caller should call ``body()`` or
``bodyChunks()`` to consume the body; otherwise, the trailers will not be available.
May return nil if there are no trailers. The trailers may be modified before they are sent
to the next filter.

Returns a :ref:`header object <config_http_filters_lua_header_wrapper>`.

.. include:: ../../../_include/lua_common.rst

``httpCall()``
^^^^^^^^^^^^^^

.. code-block:: lua

  local headers, body = handle:httpCall(cluster, headers, body, timeout_ms, asynchronous)

  -- Alternative function signature.
  local headers, body = handle:httpCall(cluster, headers, body, options)

Makes an HTTP call to an upstream host. ``cluster`` is a string which maps to a configured cluster. ``headers``
is a table of key/value pairs to send (the value can be a string or table of strings). Note that
the ``:method``, ``:path``, and ``:authority`` headers must be set. ``body`` is an optional string of body
data to send. ``timeout_ms`` is an integer that specifies the call timeout in milliseconds.

``asynchronous`` is a boolean flag. If async is set to true, Envoy will make the HTTP request and continue,
regardless of the response success or failure. If this is set to false, or not set, Envoy will suspend executing the script
until the call completes or has an error.

Returns ``headers``, which is a table of response headers. Returns ``body``, which is the string response
body. May be nil if there is no body.


The alternative function signature allows the caller to specify ``options`` as a table. Currently,
the supported keys are:

* ``asynchronous`` is a boolean flag that controls the asynchronicity of the HTTP call.
  It refers to the same ``asynchronous`` flag as the first function signature.
* ``timeout_ms`` is an integer that specifies the call timeout in milliseconds.
  It refers to the same ``timeout_ms`` argument as the first function signature.
* ``trace_sampled`` is a boolean flag that decides whether the produced trace span will be sampled or not. If not provided, the sampling decision of the parent span is used.
* ``return_duplicate_headers`` is a boolean flag that decides whether the repeated headers are allowed in response headers.
  If ``return_duplicate_headers`` is set to false (default), the returned ``headers`` is a table with value type of string.
  If ``return_duplicate_headers`` is set to true, the returned ``headers`` is a table with value type of string or value type
  of table.
* ``send_xff`` is a boolean flag that decides whether the ``x-forwarded-for`` header is sent to the target server.
  The default value is true.

  For example, the following upstream response headers have repeated headers.

  .. code-block:: none

    {
      { ":status", "200" },
      { "foo", "bar" },
      { "key", "value_0" },
      { "key", "value_1" },
      { "key", "value_2" },
    }

  Then if ``return_duplicate_headers`` is set to false, the returned headers will be:

  .. code-block:: lua

    {
      [":status"] = "200",
      ["foo"] = "bar",
      ["key"] = "value_2",
    }

  If ``return_duplicate_headers`` is set to true, the returned ``headers`` will be:

  .. code-block:: lua

    {
      [":status"] = "200",
      ["foo"] = "bar",
      ["key"] = { "value_0", "value_1", "value_2" },
    }


Some examples of specifying ``options`` are shown below:

.. code-block:: lua

  -- Create a fire-and-forget HTTP call.
  local request_options = {["asynchronous"] = true}

  -- Create a synchronous HTTP call with 1000 ms timeout.
  local request_options = {["timeout_ms"] = 1000}

  -- Create a synchronous HTTP call, but do not sample the trace span.
  local request_options = {["trace_sampled"] = false}

  -- The same as above, but explicitly set the "asynchronous" flag to false.
  local request_options = {["asynchronous"] = false, ["trace_sampled"] = false }

  -- The same as above, but with 1000 ms timeout.
  local request_options = {["asynchronous"] = false, ["trace_sampled"] = false, ["timeout_ms"] = 1000 }


``respond()``
^^^^^^^^^^^^^^

.. code-block:: lua

  handle:respond(headers, body)

Respond immediately and do not continue with further filter iteration. This call is **only valid in
the request flow**. Additionally, a response is only possible if the request headers have not yet been
passed to subsequent filters. Meaning, the following Lua code is invalid:

.. code-block:: lua

  function envoy_on_request(request_handle)
    for chunk in request_handle:bodyChunks() do
      request_handle:respond(
        {[":status"] = "100"},
        "nope")
    end
  end

``headers`` is a table of key/value pairs to send (the value can be a string or table of strings).
Note that the ``:status`` header must be set. ``body`` is a string and supplies an optional response
body. May be nil.

``metadata()``
^^^^^^^^^^^^^^

.. code-block:: lua

  local metadata = handle:metadata()

Returns the current route entry metadata. Note that the metadata should be specified
under the :ref:`filter config name
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`.
If no entry could be found by the filter config name, then the filter canonical name
i.e. ``envoy.filters.http.lua`` will be used as an alternative. Note that this downgrade will be
deprecated in the future.

.. note::

  This method will be deprecated in the future. In order to access route configuration,
  consider using :ref:`route object's metadata() <config_http_filters_lua_route_wrapper_metadata>` instead,
  which provides more consistent behavior. **Important**: route object's ``metadata()`` requires
  metadata to be configured under the exact filter name and does not fall back to the
  canonical name ``envoy.filters.http.lua``.

Below is an example of a ``metadata`` in a :ref:`route entry <envoy_v3_api_msg_config.route.v3.Route>`.

.. literalinclude:: _include/lua-filter.yaml
    :language: yaml
    :lines: 33-39
    :lineno-start: 33
    :linenos:
    :caption: :download:`lua-filter.yaml <_include/lua-filter.yaml>`

Returns a :ref:`metadata object <config_http_filters_lua_metadata_wrapper>`.

``streamInfo()``
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local streamInfo = handle:streamInfo()

Returns :repo:`information <envoy/stream_info/stream_info.h>` related to the current request.

Returns a :ref:`stream info object <config_http_filters_lua_stream_info_wrapper>`.

``connection()``
^^^^^^^^^^^^^^^^

.. code-block:: lua

  local connection = handle:connection()

Returns the current request's underlying :repo:`connection <envoy/network/connection.h>`.

Returns a :ref:`connection object <config_http_filters_lua_connection_wrapper>`.

``connectionStreamInfo()``
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local connectionStreamInfo = handle:connectionStreamInfo()

Returns connection-level :repo:`information <envoy/stream_info/stream_info.h>` related to the current request.

Returns a connection-level :ref:`stream info object <config_http_filters_lua_cx_stream_info_wrapper>`.

``setUpstreamOverrideHost()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  handle:setUpstreamOverrideHost(host, strict)

Sets an upstream address override for the request. When the overridden host is available and can be selected directly,
the load balancer bypasses its algorithm and routes traffic directly to the specified host. The strict flag determines
whether the HTTP request must strictly use the overridden destination. If the destination is unavailable and strict is
set to true, Envoy responds with a 503 Service Unavailable error.

The function takes two arguments:

* ``host`` (string): The upstream host address to use for the request. This must be a valid IP address; otherwise, the
  Lua script will throw an error.
* ``strict`` (boolean, optional): Determines whether the HTTP request must be strictly routed to the requested
  destination. When set to ``true``, if the requested destination is unavailable, Envoy will return a 503 status code.
  The default value is ``false``, which allows Envoy to fall back to its load balancing mechanism. In this case, if the
  requested destination is not found, the request will be routed according to the load balancing algorithm.

Example:

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Override upstream host without strict mode
    request_handle:setUpstreamOverrideHost("192.168.21.13", false)

    -- Override upstream host with strict mode
    request_handle:setUpstreamOverrideHost("192.168.21.13", true)
  end

.. _config_http_filters_lua_stream_handle_api_clear_route_cache:

``clearRouteCache()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  handle:clearRouteCache()

Clears the route cache for the current request. This will force the route to be recomputed. If you
updated the request headers, metadata, or other information that affects the route and expect the route
to be recomputed, you can call this function to clear the route cache. Then the route will be recomputed
when the route is accessed the next time.

Example:

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Clear the route cache
    request_handle:clearRouteCache()
  end

.. _config_http_filters_lua_stream_handle_api_filter_context:

``filterContext()``
^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local filter_context = handle:filterContext()

Returns the filter context that is configured in the
:ref:`filter_context <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.filter_context>`.

For example, given the following filter context in the route entry:

.. code-block:: yaml

  typed_per_filter_config:
    "lua-filter-name":
      "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
      filter_context:
        key: xxxxxx

The filter context can be accessed in the related Lua script as follows:

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Get the filter context
    local filter_context = request_handle:filterContext()

    -- Access the filter context data
    local value = filter_context["key"]
  end

``importPublicKey()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local pubkey = handle:importPublicKey(keyder, keyderLength)

Returns a public key which is used by :ref:`verifySignature <verify_signature>` to verify a digital signature.

.. _verify_signature:

``verifySignature()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local ok, error = handle:verifySignature(hashFunction, pubkey, signature, signatureLength, data, dataLength)

Verifies a signature using the provided parameters. ``hashFunction`` is the variable for the hash function which will be used
for verifying the signature. ``SHA1``, ``SHA224``, ``SHA256``, ``SHA384`` and ``SHA512`` are supported.
``pubkey`` is the public key. ``signature`` is the signature to be verified. ``signatureLength`` is
the length of the signature. ``data`` is the content which will be hashed. ``dataLength`` is the length of the data.

The function returns a pair. If the first element is ``true``, the second element will be empty,
which means the signature is verified; otherwise, the second element will store the error message.

.. _config_http_filters_lua_stream_handle_api_base64_escape:

``base64Escape()``
^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local base64_encoded = handle:base64Escape("input string")

Encodes the input string as base64. This can be useful for escaping binary data.

``timestamp()``
^^^^^^^^^^^^^^^

.. code-block:: lua

  timestamp = handle:timestamp(format)

High-resolution timestamp function. ``format`` is an optional enum parameter to indicate the format of the timestamp.
``EnvoyTimestampResolution.MILLISECOND`` is supported.
The function returns a timestamp in milliseconds since epoch by default if format is not set.

.. _config_http_filters_lua_stream_handle_api_timestamp_string:

``timestampString()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  timestamp = handle:timestampString(resolution)

Timestamp function. The timestamp is returned as a string. It represents the integer value of the selected resolution
since epoch. ``resolution`` is an optional enum parameter to indicate the resolution of the timestamp.
Supported resolutions are ``EnvoyTimestampResolution.MILLISECOND`` and ``EnvoyTimestampResolution.MICROSECOND``.
The default resolution is millisecond if ``resolution`` is not set.

.. _config_http_filters_lua_stream_handle_api_virtual_host:

``virtualHost()``
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local virtual_host = handle:virtualHost()

Returns a virtual host object that provides access to the virtual host configuration. This method always returns
a valid object, even when the request does not match any configured virtual host. However, if no virtual host
matches, calling methods on the returned object will return ``nil`` or, in the case of the ``metadata()`` method,
an empty metadata object.

Returns a :ref:`virtual host object <config_http_filters_lua_virtual_host_wrapper>`.

.. _config_http_filters_lua_stream_handle_api_route:

``route()``
^^^^^^^^^^^

.. code-block:: lua

  local route = handle:route()

Returns a route object that provides access to the route configuration. This method always returns
a valid object, even when the request does not match any configured route. However, if no route
matches, calling methods on the returned object will return ``nil`` or, in the case of the ``metadata()`` method,
an empty metadata object.

Returns a :ref:`route object <config_http_filters_lua_route_wrapper>`.

.. _config_http_filters_lua_header_wrapper:

Header object API
-----------------

.. include:: ../../../_include/lua_common.rst

``add()``
^^^^^^^^^

.. code-block:: lua

  headers:add(key, value)

Adds a header. ``key`` is a string that supplies the header key. ``value`` is a string that supplies
the header value.

``get()``
^^^^^^^^^

.. code-block:: lua

  headers:get(key)

Gets a header. ``key`` is a string that supplies the header key. Returns a string that is the header
value or nil if there is no such header. If there are multiple headers in the same case-insensitive
key, their values will be combined with a ``,`` separator and returned as a string.

``getAtIndex()``
^^^^^^^^^^^^^^^^

.. code-block:: lua

  headers:getAtIndex(key, index)

Gets the header value at the given index. It can be used to fetch a specific value in case the
given header has multiple values. ``key`` is a string that supplies the header key and index is
an integer that supplies the position. It returns a string that is the header value or nil if
there is no such header or if there is no value at the specified index.

``getNumValues()``
^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  headers:getNumValues(key)

Gets the number of values of a given header. It can be used to fetch the total number of values in case
the given header has multiple values. ``key`` is a string that supplies the header key. It returns
an integer with the value size for the given header or ``0`` if there is no such header.

``__pairs()``
^^^^^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(headers) do
  end

Iterates through every header. ``key`` is a string that supplies the header key. ``value`` is a string
that supplies the header value.

.. attention::

  In the current implementation, headers cannot be modified during iteration. Additionally, if
  it is necessary to modify headers after an iteration, the iteration must first be completed. This means that
  ``break`` or any other way to exit the loop early must not be used. This may be more flexible in the future.

``remove()``
^^^^^^^^^^^^

.. code-block:: lua

  headers:remove(key)

Removes a header. ``key`` supplies the header key to remove.

``replace()``
^^^^^^^^^^^^^

.. code-block:: lua

  headers:replace(key, value)

Replaces a header. ``key`` is a string that supplies the header key. ``value`` is a string that supplies
the header value. If the header does not exist, it is added as per the ``add()`` function.

``setHttp1ReasonPhrase()``
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  headers:setHttp1ReasonPhrase(reasonPhrase)

Sets a custom HTTP/1 response reason phrase. This call is **only valid in the response flow**.
``reasonPhrase`` is a string that supplies the reason phrase value. Additionally this call only
affects HTTP/1 connections. It will have no effect if the client is HTTP/2 or HTTP/3.

.. _config_http_filters_lua_buffer_wrapper:

Buffer API
----------

.. include:: ../../../_include/lua_common.rst

``length()``
^^^^^^^^^^^^

.. code-block:: lua

  local size = buffer:length()

Gets the size of the buffer in bytes. Returns an integer.

``getBytes()``
^^^^^^^^^^^^^^

.. code-block:: lua

  buffer:getBytes(index, length)

Gets bytes from the buffer. By default Envoy will not copy all buffer bytes to Lua. This will
cause a buffer segment to be copied. ``index`` is an integer and supplies the buffer start index to
copy. ``length`` is an integer and supplies the buffer length to copy. ``index`` + ``length`` must be
less than the buffer length.

.. _config_http_filters_lua_buffer_wrapper_api_set_bytes:

``setBytes()``
^^^^^^^^^^^^^^

.. code-block:: lua

  buffer:setBytes(string)

Set the content of wrapped buffer with the input string.

.. _config_http_filters_lua_metadata_wrapper:

Metadata object API
-------------------

.. include:: ../../../_include/lua_common.rst

``get()``
^^^^^^^^^

.. code-block:: lua

  metadata:get(key)

Gets metadata. ``key`` is a string that supplies the metadata key. Returns the corresponding
value of the given metadata key. The type of the value can be: ``nil``, ``boolean``, ``number``,
``string`` and ``table``.

``__pairs()``
^^^^^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(metadata) do
  end

Iterates through every ``metadata`` entry. ``key`` is a string that supplies a ``metadata``
key. ``value`` is a ``metadata`` entry value.

.. _config_http_filters_lua_stream_info_wrapper:

Stream info object API
-----------------------

.. include:: ../../../_include/lua_common.rst

``protocol()``
^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:protocol()

Returns the string representation of :repo:`HTTP protocol <envoy/http/protocol.h>`
used by the current request. The possible values are: ``HTTP/1.0``, ``HTTP/1.1``, ``HTTP/2`` and ``HTTP/3``.

.. _config_http_filters_lua_stream_info_route_name:

``routeName()``
^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:routeName()

Returns the name of the route matched by the filter chain. Returns an empty string if no route was matched.

Example usage:

.. code-block:: lua

  function envoy_on_request(request_handle)
    local route_name = request_handle:streamInfo():routeName()
    request_handle:logInfo("Matched route: " .. route_name)
  end

.. _config_http_filters_lua_stream_info_virtual_cluster_name:

``virtualClusterName()``
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:virtualClusterName()

Returns the name of the virtual cluster matched for the current request. Returns an empty string if no virtual cluster
was matched.

Example usage:

.. code-block:: lua

  function envoy_on_request(request_handle)
    local virtual_cluster = request_handle:streamInfo():virtualClusterName()
    request_handle:logInfo("Matched virtual cluster: " .. virtual_cluster)
  end

.. _config_http_filters_lua_stream_info_downstream_direct_local_address:

``downstreamDirectLocalAddress()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamDirectLocalAddress()

Returns the string representation of :repo:`downstream direct local address <envoy/stream_info/stream_info.h>`
used by the current request. This is always the physical local address of the connection.

``downstreamLocalAddress()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamLocalAddress()

Returns the string representation of :repo:`downstream local address <envoy/stream_info/stream_info.h>`
used by the current request.

.. _config_http_filters_lua_stream_info_downstream_direct_remote_address:

``downstreamDirectRemoteAddress()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamDirectRemoteAddress()

Returns the string representation of :repo:`downstream directly connected address <envoy/stream_info/stream_info.h>`
used by the current request. This is equivalent to the address of the physical connection.

.. _config_http_filters_lua_stream_info_downstream_remote_address:

``downstreamRemoteAddress()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamRemoteAddress()

Returns the string representation of the downstream remote address for the current request. This may differ from
:ref:`downstreamDirectRemoteAddress() <config_http_filters_lua_stream_info_downstream_direct_remote_address>` depending upon the setting of
:ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>`.

``dynamicMetadata()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:dynamicMetadata()

Returns a :ref:`dynamic metadata object <config_http_filters_lua_stream_info_dynamic_metadata_wrapper>`.

``dynamicTypedMetadata()``
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:dynamicTypedMetadata(filterName)

Returns dynamic typed metadata for a given filter name. This provides type-safe access to metadata values that are stored as protocol buffer messages, particularly useful when working with HTTP filters that store structured data.

``filterName`` is a string that supplies the filter name, e.g. ``envoy.filters.http.set_metadata``. Returns a Lua table containing the unpacked protocol buffer message. Returns nil if no dynamic metadata exists for the given filter name or if the metadata cannot be unpacked.

.. include:: _include/lua_dynamic_typed_metadata_common.rst

**Common Use Cases:**

1. **Accessing Set Metadata Filter Data:**

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Access typed metadata set by the set_metadata filter
    local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("envoy.filters.http.set_metadata")

    -- Check if metadata exists
    if typed_meta then
      -- Access specific fields
      local metadata_namespace = typed_meta.metadata_namespace
      local allow_overwrite = typed_meta.allow_overwrite

      request_handle:logInfo(string.format("Metadata namespace: %s, Allow overwrite: %s",
                                          metadata_namespace or "none", tostring(allow_overwrite)))
    else
      request_handle:logInfo("No set_metadata typed metadata available")
    end
  end

2. **Working with External Processing Filter Metadata:**

.. code-block:: lua

  function envoy_on_request(request_handle)
    local metadata = request_handle:streamInfo():dynamicTypedMetadata("envoy.filters.http.ext_proc")

    -- Check if metadata exists before accessing
    if metadata then
      -- Safely access potentially nested fields
      if metadata.processing_mode then
        -- Access processing mode configuration
        if metadata.processing_mode.request_header_mode then
          request_handle:logInfo(string.format("Request header mode: %s", metadata.processing_mode.request_header_mode))
        end

        -- Access grpc service configuration
        if metadata.grpc_service and metadata.grpc_service.envoy_grpc then
          request_handle:logInfo(string.format("Cluster name: %s", metadata.grpc_service.envoy_grpc.cluster_name))
        end
      end
    else
      request_handle:logInfo("No ext_proc typed metadata available")
    end
  end

``filterState()``
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:filterState()

Returns a :ref:`filter state object <config_http_filters_lua_stream_info_filter_state_wrapper>` that provides access to objects stored by filters during request processing.

Filter state contains data shared between filters, such as routing decisions, authentication results, rate limiting state, and other processing information.

Example usage:

.. code-block:: lua

  function envoy_on_request(request_handle)
    local filter_state = request_handle:streamInfo():filterState()

    -- Get authentication result
    local auth_result = filter_state:get("auth.result")
    if auth_result then
      request_handle:headers():add("x-auth-result", auth_result)
    end

    -- Check rate limiting decision
    local rate_limit_remaining = filter_state:get("rate_limit.remaining")
    if rate_limit_remaining and rate_limit_remaining < 10 then
      request_handle:headers():add("x-rate-limit-warning", "low")
    end
  end

``downstreamSslConnection()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamSslConnection()

Returns :repo:`information <envoy/ssl/connection.h>` related to the current SSL connection.

Returns a downstream :ref:`SSL connection info object <config_http_filters_lua_ssl_socket_info>`.

.. _config_http_filters_lua_stream_info_dynamic_metadata_wrapper:

``requestedServerName()``
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:requestedServerName()

Returns the string representation of :repo:`requested server name <envoy/stream_info/stream_info.h>`
(e.g. SNI in TLS) for the current request if present.

``drainConnectionUponCompletion()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:drainConnectionUponCompletion()

Marks the connection to be drained upon completion of the current request.

* For HTTP/1.1, this will add a ``Connection: close`` header to the response.
* For HTTP/2 and HTTP/3, this will trigger the sending of a ``GOAWAY`` frame.

This is useful when you want to force clients to re-establish connections, for example:

* After authorization failures to ensure clients reconnect with updated credentials.
* When detecting network changes that may affect connection validity.
* To implement custom connection lifecycle policies.

Example usage:

.. code-block:: lua

  function envoy_on_response(response_handle)
    -- Check for status from upstream and force connection drain on authorization failure.
    local status_header = response_handle:headers():get(":status")
    if status_header == "403" then
      response_handle:streamInfo():drainConnectionUponCompletion()
    end
  end

.. _config_http_filters_lua_cx_stream_info_wrapper:

Connection stream info object API
---------------------------------

.. include:: ../../../_include/lua_common.rst

``dynamicTypedMetadata()``
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  connectionStreamInfo:dynamicTypedMetadata(filterName)

Returns dynamic metadata for a given filter name. Dynamic metadata provides type-safe access to metadata values that are stored as protocol buffer messages. This is particularly useful when working with filters that store structured data.

``filterName`` is a string that supplies the filter name, e.g. ``envoy.lb``. Returns a Lua table containing the unpacked protocol buffer message. Returns nil if no dynamic metadata exists for the given filter name or if the metadata cannot be unpacked.

.. include:: _include/lua_dynamic_typed_metadata_common.rst

**Common Use Cases:**

1. **Accessing Proxy Protocol Metadata:**

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- Access proxy protocol typed metadata
    local ppv2_metadata = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.filters.listener.proxy_protocol")

    -- Check if typed metadata exists
    if ppv2_metadata then
      -- Access TLV values
      local ppv2_typed_metadata = ppv2_metadata.typed_metadata

      -- Check if TLV values exist
      if ppv2_typed_metadata then
        for tlv_key, tlv_value in pairs(ppv2_typed_metadata) do
          -- Log each TLV key and value
          request_handle:logInfo(string.format("TLV: %s, Value: %s", tlv_key or "none", request_handle:base64Escape(tlv_value) or "none"))
        end
      else
        request_handle:logDebug("No typed metadata found in proxy protocol metadata.")
      end
    else
      request_handle:logInfo("No proxy protocol metadata available.")
    end
  end

2. **Working with Custom Filter Metadata:**

.. code-block:: lua

  function envoy_on_request(request_handle)
    local metadata = request_handle:connectionStreamInfo():dynamicTypedMetadata("custom.filter")

    -- Check if metadata exists before accessing
    if metadata then
      -- Safely access potentially nested fields
      if metadata.config then
        -- Access nested configuration
        if metadata.config.rules then
          for _, rule in ipairs(metadata.config.rules) do
            if rule.name and rule.value then
              request_handle:logInfo(string.format("Rule: %s = %s", rule.name, rule.value))
            end
          end
        end

        -- Access map fields
        if metadata.config.properties then
          for key, value in pairs(metadata.config.properties) do
            request_handle:logInfo(string.format("Property: %s = %s", key, value))
          end
        end
      end
    else
      request_handle:logInfo("No metadata available for custom.filter")
    end
  end

``dynamicMetadata()``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  connectionStreamInfo:dynamicMetadata()

Returns a :ref:`dynamic metadata object <config_http_filters_lua_stream_info_dynamic_metadata_wrapper>`.

Dynamic metadata object API
---------------------------

.. include:: ../../../_include/lua_common.rst

``get()``
^^^^^^^^^

.. code-block:: lua

  dynamicMetadata:get(filterName)

  -- to get a value from a returned table.
  dynamicMetadata:get(filterName)[key]

Gets an entry in the dynamic metadata struct. ``filterName`` is a string that supplies the filter name, e.g. ``envoy.lb``.
Returns the corresponding ``table`` of a given ``filterName``.

``set()``
^^^^^^^^^

.. code-block:: lua

  dynamicMetadata:set(filterName, key, value)

Sets a key-value pair of a ``filterName``'s metadata. ``filterName`` is a key specifying the target filter name,
e.g. ``envoy.lb``. The type of ``key`` is ``string``. The type of ``value`` is any Lua type that can be mapped
to a metadata value: ``table``, ``numeric``, ``boolean``, ``string`` or ``nil``. When using a ``table`` as an argument,
its keys can only be ``string`` or ``numeric``.

.. code-block:: lua

  function envoy_on_request(request_handle)
    local headers = request_handle:headers()
    request_handle:streamInfo():dynamicMetadata():set("envoy.filters.http.lua", "request.info", {
      auth = headers:get("authorization"),
      token = headers:get("x-request-token"),
    })
  end

  function envoy_on_response(response_handle)
    local meta = response_handle:streamInfo():dynamicMetadata():get("envoy.filters.http.lua")["request.info"]
    response_handle:logInfo("Auth: "..meta.auth..", token: "..meta.token)
  end


``__pairs()``
^^^^^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(dynamicMetadata) do
  end

Iterates through every ``dynamicMetadata`` entry. ``key`` is a string that supplies a ``dynamicMetadata``
key. ``value`` is a ``dynamicMetadata`` entry value.

.. _config_http_filters_lua_stream_info_filter_state_wrapper:

Filter state object API
------------------------

.. include:: ../../../_include/lua_common.rst

``get()``
^^^^^^^^^

.. code-block:: lua

  filterState:get(objectName)
  filterState:get(objectName, fieldName)

Gets a filter state object by name with optional field access. ``objectName`` is a string that specifies the name of the filter state object to retrieve. ``fieldName`` is an optional string that specifies a field name for objects that support field access.

Returns the filter state value as a string. Returns ``nil`` if the object does not exist, cannot be serialized, or if the specified field doesn't exist.

Objects that support field access can have specific fields retrieved using the optional second parameter.

.. code-block:: lua

  function envoy_on_request(request_handle)
    local filter_state = request_handle:streamInfo():filterState()

    -- All values returned as strings
    local auth_token = filter_state:get("auth.token")
    if auth_token then
      request_handle:headers():add("x-auth-token", auth_token)
    end

    -- Boolean-like string values
    local is_authenticated = filter_state:get("auth.authenticated")
    if is_authenticated == "true" then
      request_handle:headers():add("x-authenticated", "yes")
    end

    -- Access specific fields from objects that support field access
    local user_name = filter_state:get("user.info", "name")
    if user_name then
      request_handle:headers():add("x-user-name", user_name)
    end

    local user_id_str = filter_state:get("user.info", "id")
    if user_id_str then
      local user_id = tonumber(user_id_str)
      if user_id and user_id > 1000 then
        request_handle:headers():add("x-premium-user", "true")
      end
    end
  end

.. _config_http_filters_lua_connection_wrapper:

Connection object API
---------------------

.. include:: ../../../_include/lua_common.rst

``ssl()``
^^^^^^^^^

.. code-block:: lua

  if connection:ssl() == nil then
    print("plain")
  else
    print("secure")
  end

Returns :repo:`SSL connection <envoy/ssl/connection.h>` object when the connection is
secured and ``nil`` when it is not.

Returns an :ref:`SSL connection info object <config_http_filters_lua_ssl_socket_info>`.

.. _config_http_filters_lua_ssl_socket_info:

SSL connection object API
-------------------------

.. include:: ../../../_include/lua_common.rst

``peerCertificatePresented()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificatePresented() then
    print("peer certificate is presented")
  end

Returns a bool representing whether the peer certificate is presented.

``peerCertificateValidated()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificateValidated() then
    print("peer certificate is validated")
  end

Returns a bool whether the peer certificate was validated.

.. warning::

   Client certificate validation is not currently performed upon TLS session resumption. For a
   resumed TLS session this method will return false, regardless of whether the peer certificate is
   valid.

   The only known workaround for this issue is to disable TLS session resumption entirely, by
   setting both :ref:`disable_stateless_session_resumption <envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.disable_stateless_session_resumption>`
   and :ref:`disable_stateful_session_resumption <envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.disable_stateful_session_resumption>` on the DownstreamTlsContext.

``uriSanLocalCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  -- For example, uriSanLocalCertificate contains {"san1", "san2"}
  local certs = downstreamSslConnection:uriSanLocalCertificate()

  -- The following prints san1,san2
  handle:logTrace(table.concat(certs, ","))

Returns the URIs (as a table) in the SAN field of the local certificate. Returns an empty table if
there is no local certificate, or no SAN field, or no URI SAN entries.

``sha256PeerCertificateDigest()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sha256PeerCertificateDigest()

Returns the SHA256 digest of the peer certificate. Returns ``""`` if there is no peer certificate
which can happen in TLS (non-mTLS) connections.

``serialNumberPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:serialNumberPeerCertificate()

Returns the serial number field of the peer certificate. Returns ``""`` if there is no peer
certificate, or no serial number.

``issuerPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:issuerPeerCertificate()

Returns the issuer field of the peer certificate in RFC 2253 format. Returns ``""`` if there is no
peer certificate, or no issuer.

``subjectPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectPeerCertificate()

Returns the subject field of the peer certificate in RFC 2253 format. Returns ``""`` if there is no
peer certificate, or no subject.

``parsedSubjectPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local parsedSubject = downstreamSslConnection:parsedSubjectPeerCertificate()
  if parsedSubject then
    print("CN: " .. parsedSubject:commonName())
    print("O: " .. table.concat(parsedSubject:organizationName(), ","))
  end

Returns :repo:`connection <envoy/ssl/parsed_x509_name.h>` parsed from the subject field of the peer
certificate. Returns nil if there is no peer certificate.

Returns a :ref:`parsed name object <config_http_filters_lua_parsed_name>`.

``uriSanPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:uriSanPeerCertificate()

Returns the URIs (as a table) in the SAN field of the peer certificate. Returns an empty table if
there is no peer certificate, or no SAN field, or no URI SAN entries.

``subjectLocalCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectLocalCertificate()

Returns the subject field of the local certificate in RFC 2253 format. Returns ``""`` if there is no
local certificate, or no subject.

``urlEncodedPemEncodedPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificate()

Returns the URL-encoded PEM-encoded representation of the peer certificate. Returns ``""`` if there
is no peer certificate or encoding fails.

``urlEncodedPemEncodedPeerCertificateChain()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificateChain()

Returns the URL-encoded PEM-encoded representation of the full peer certificate chain including the
leaf certificate. Returns ``""`` if there is no peer certificate or encoding fails.

``dnsSansPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansPeerCertificate()

Returns the DNS entries (as a table) in the SAN field of the peer certificate. Returns an empty
table if there is no peer certificate, or no SAN field, or no DNS SAN entries.

``dnsSansLocalCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansLocalCertificate()

Returns the DNS entries (as a table) in the SAN field of the local certificate. Returns an empty
table if there is no local certificate, or no SAN field, or no DNS SAN entries.

``oidsPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:oidsPeerCertificate()

Returns the string representation of OIDs (as a table) from the peer certificate. This is for
reading the OID strings from the certificate, not the extension values associated with OIDs.
Returns an empty table if there is no peer certificate or no OIDs.

``oidsLocalCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:oidsLocalCertificate()

Returns the string representation of OIDs (as a table) from the local certificate. This is for
reading the OID strings from the certificate, not the extension values associated with OIDs.
Returns an empty table if there is no local certificate or no OIDs.

``validFromPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

Returns the time (timestamp-since-epoch in seconds) that the peer certificate was issued and should
be considered valid from. Returns ``0`` if there is no peer certificate.

In Lua, we usually use ``os.time(os.date("!*t"))`` to get the current timestamp-since-epoch in seconds.

``expirationPeerCertificate()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

Returns the time (timestamp-since-epoch in seconds) that the peer certificate expires and should not
be considered valid after. Returns ``0`` if there is no peer certificate.

In Lua, we usually use ``os.time(os.date("!*t"))`` to get the current timestamp-since-epoch in seconds.

``sessionId()``
^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sessionId()

Returns the hex-encoded TLS session ID as defined in RFC 5246.

``ciphersuiteId()``
^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteId()

Returns the standard ID (hex-encoded) for the ciphers used in the established TLS connection.
Returns ``"0xffff"`` if there is no current negotiated ciphersuite.

``ciphersuiteString()``
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteString()

Returns the OpenSSL name for the set of ciphers used in the established TLS connection. Returns
``""`` if there is no current negotiated ciphersuite.

``tlsVersion()``
^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:tlsVersion()

Returns the TLS version (e.g., TLSv1.2, TLSv1.3) used in the established TLS connection.

.. _config_http_filters_lua_parsed_name:

Parsed name object API
----------------------

.. include:: ../../../_include/lua_common.rst

``commonName()``
^^^^^^^^^^^^^^^^

.. code-block:: lua

  parsedSubject:commonName()

Returns the string representation of the CN field from the X.509 name. Returns ``""`` if there is no such
field or if the field can't be converted to a UTF8 string.

``organizationName()``
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  parsedSubject:organizationName()

Returns the string representation of O fields (as a table) from the X.509 name. Returns an empty
table if there is no such field or if the field can't be converted to a UTF8 string.

.. _config_http_filters_lua_virtual_host_wrapper:

Virtual host object API
-----------------------

.. include:: ../../../_include/lua_common.rst

``metadata()``
^^^^^^^^^^^^^^

.. code-block:: lua

  local metadata = virtual_host:metadata()

Returns the virtual host metadata. Note that the metadata should be specified
under the :ref:`filter config name
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`.

Below is an example of a ``metadata`` in a :ref:`route entry <envoy_v3_api_msg_config.route.v3.VirtualHost>`.

.. literalinclude:: _include/lua-filter.yaml
    :language: yaml
    :lines: 20-26
    :lineno-start: 20
    :linenos:
    :caption: :download:`lua-filter.yaml <_include/lua-filter.yaml>`

Returns a :ref:`metadata object <config_http_filters_lua_metadata_wrapper>`.

.. _config_http_filters_lua_route_wrapper:

Route object API
----------------

.. include:: ../../../_include/lua_common.rst

.. _config_http_filters_lua_route_wrapper_metadata:

``metadata()``
^^^^^^^^^^^^^^

.. code-block:: lua

  local metadata = route:metadata()

Returns the route metadata. Note that the metadata should be specified
under the :ref:`filter config name
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`.

Below is an example of a ``metadata`` in a :ref:`route entry <envoy_v3_api_msg_config.route.v3.Route>`.

.. literalinclude:: _include/lua-filter.yaml
    :language: yaml
    :lines: 33-39
    :lineno-start: 33
    :linenos:
    :caption: :download:`lua-filter.yaml <_include/lua-filter.yaml>`

Returns a :ref:`metadata object <config_http_filters_lua_metadata_wrapper>`.
