.. _config_http_cluster_specifier_lua:

Lua cluster specifier
=====================

Overview
--------

The HTTP Lua cluster specifier allows `Lua <https://www.lua.org/>`_ scripts to select a router cluster
during the request flow.

.. note::
   `LuaJIT <https://luajit.org/>`_ is used as the runtime.

   This means that the currently supported Lua version is mostly 5.1 with some 5.2 features.

   See the `LuaJIT documentation <https://luajit.org/extensions.html>`_ for more details.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.lua.v3.LuaConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.lua.v3.LuaConfig>`

A simple example configuration of a Lua cluster:

.. literalinclude:: /start/sandboxes/_include/lua-cluster-specifier/envoy.yaml
    :language: yaml
    :lines: 22-40
    :emphasize-lines: 3-17
    :linenos:
    :caption: :download:`lua-cluster-specifier.yaml </start/sandboxes/_include/lua-cluster-specifier/envoy.yaml>`

The Lua script defined in
:ref:`source_code <envoy_v3_api_field_extensions.router.cluster_specifiers.lua.v3.LuaConfig.source_code>`
will be executed to select the routed cluster.

It can also select a cluster based on matched request headers.

If execution of the Lua script results in failure, the
:ref:`default_cluster <envoy_v3_api_field_extensions.router.cluster_specifiers.lua.v3.LuaConfig.default_cluster>`
will be used.

Complete example
----------------

A complete example using Docker is available in the :ref:`Lua cluster specifier sandbox <install_sandboxes_lua_cluster_specifier>`.

Route handle API
----------------

When Envoy loads the script in the configuration, it looks for a global function defined by the script:

.. code-block:: lua

  function envoy_on_route(route_handle)
  end

Following the route path, Envoy will run ``envoy_on_route`` as a coroutine, passing a handle to the route API.

The following method on the stream handle is supported:

``headers()``
+++++++++++++

.. code-block:: lua

  local headers = route_handle:headers()

Returns the stream's headers. The headers can be used to select a specific cluster.

Returns a :ref:`header object <config_lua_cluster_specifier_header_wrapper>`.

.. _config_lua_cluster_specifier_header_wrapper:

Header object API
-----------------

``get()``
+++++++++

.. code-block:: lua

  headers:get(key)

This method gets a header.

``key`` is a string that specifies the header key.

Returns either a string containing the header value, or ``nil`` if the header does not exist.

If there are multiple headers in the same case-insensitive key, their values will be concatenated to a string separated by ``,``.
