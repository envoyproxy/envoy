.. _config_http_cluster_specifier_lua:

Lua cluster specifier
========================

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

  name: envoy.router.cluster_specifier_plugin.lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.lua.v3.LuaConfig
    source_code:
      inline_string: |
        function envoy_on_cluster(header_handle)
          local header_value = header_handle:get("header_key")
          if header_value == "fake" then
            return "fake_service"
          end
          return "web_service"
        end
    default_cluster: web_service

Lua script defined in ``source_code`` will be executed to select router cluster, and just as cluster specifier
plugin in C++, Lua script can also select router cluster based on request headers. If Lua script has no
``envoy_on_cluster`` function or execute failure, ``default_cluster`` will be used.

Complete example
----------------

A complete example using Docker is available in :repo:`/examples/lua-cluster-specifier`.
