Added ``requestHeaders()`` to the Lua HTTP filter's stream handle, allowing a script to read the
request headers from ``envoy_on_response`` as well as ``envoy_on_request``. Returns ``nil`` if the
stream has no request headers.
