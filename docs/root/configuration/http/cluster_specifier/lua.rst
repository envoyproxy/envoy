.. _config_http_cluster_specifier_lua:

Lua cluster specifier
=====================

Overview
--------

The HTTP Lua cluster specifier allows `Lua <https://www.lua.org/>`_ scripts to select router cluster
during the request flows. `LuaJIT <https://luajit.org/>`_ is used as the runtime. Because of this, the
supported Lua version is mostly 5.1 with some 5.2 features. See the `LuaJIT documentation
<https://luajit.org/extensions.html>`_ for more details.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.lua.v3.LuaConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.lua.v3.LuaConfig>`

A simple example of configuring Lua cluster specifier is as follow:

.. code-block:: yaml

  routes:
  - match:
      prefix: "/"
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: envoy.router.cluster_specifier_plugin.lua
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.lua.v3.LuaConfig
            source_code:
              inline_string: |
                function envoy_on_route(route_handle)
                  local header_value = route_handle:headers():get("header_key")
                  if header_value == "fake" then
                    return "fake_service"
                  end
                  return "web_service"
                end
            default_cluster: web_service

Lua script defined in ``source_code`` will be executed to select router cluster, and just as cluster specifier
plugin in C++, Lua script can also select router cluster based on request headers. If Lua script execute failure,
``default_cluster`` will be used.

Complete example
----------------

A complete example using Docker is available in :repo:`/examples/lua-cluster-specifier`.

Route handle API
----------------

When Envoy loads the script in the configuration, it looks for a global function that the script defines:

.. code-block:: lua

  function envoy_on_route(route_handle)
  end

During the route path, Envoy will run *envoy_on_route* as a coroutine, passing a handle to the route API.

The following methods on the stream handle are supported:

headers()
^^^^^^^^^

.. code-block:: lua

  local headers = handle:headers()

Returns the stream's headers. The headers can be used to match to select a specific cluster.

Returns a :ref:`header object <config_lua_cluster_specifier_header_wrapper>`.

.. _config_lua_cluster_specifier_header_wrapper:

Header object API
-----------------

get()
^^^^^

.. code-block:: lua

  headers:get(key)

Gets a header. *key* is a string that supplies the header key. Returns a string that is the header
value or nil if there is no such header. If there are multiple headers in the same case-insensitive
key, their values will be combined with a *,* separator and returned as a string.
