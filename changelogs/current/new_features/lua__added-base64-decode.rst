Added the :ref:`base64Decode()
<config_http_filters_lua_stream_handle_api_base64_decode>` method to the HTTP Lua filter's stream
handle, the inverse of the existing ``base64Escape()``. It returns ``nil`` when the input is not
valid base64.
