Added an optional ``ns`` parameter to the ``metadata()`` methods of the HTTP Lua filter's
:ref:`stream handle <config_http_filters_lua_stream_handle_api>` and
:ref:`route object <config_http_filters_lua_route_wrapper_metadata>`. It lets a script read the
route metadata under any namespace rather than only the one named by the filter config name.
